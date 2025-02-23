//
// Created by sanhuazhang on 2019/05/02
//

/*
 * Tencent is pleased to support the open source community by making
 * WCDB available.
 *
 * Copyright (C) 2017 THL A29 Limited, a Tencent company.
 * All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use
 * this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *       https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <WCDB/Assertion.hpp>
#include <WCDB/BusyRetryConfig.hpp>
#include <WCDB/CoreConst.h>
#include <WCDB/InnerHandle.hpp>
#include <WCDB/Time.hpp>

namespace WCDB {

BusyRetryConfig::BusyRetryConfig()
: Config(), m_identifier(StringView::formatted("Busy-%p", this))
{
    Global::shared().setNotificationForLockEvent(
    m_identifier,
    std::bind(&BusyRetryConfig::willLock, this, std::placeholders::_1, std::placeholders::_2),
    std::bind(&BusyRetryConfig::lockDidChange, this, std::placeholders::_1, std::placeholders::_2),
    std::bind(
    &BusyRetryConfig::willShmLock, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
    std::bind(&BusyRetryConfig::shmLockDidChange,
              this,
              std::placeholders::_1,
              std::placeholders::_2,
              std::placeholders::_3,
              std::placeholders::_4));
}

BusyRetryConfig::~BusyRetryConfig()
{
    Global::shared().setNotificationForLockEvent(
    m_identifier, nullptr, nullptr, nullptr, nullptr);
}

bool BusyRetryConfig::invoke(InnerHandle* handle)
{
    handle->setNotificationWhenBusy(std::bind(
    &BusyRetryConfig::onBusy, this, std::placeholders::_1, std::placeholders::_2));
    return true;
}

bool BusyRetryConfig::uninvoke(InnerHandle* handle)
{
    handle->setNotificationWhenBusy(nullptr);
    return true;
}

bool BusyRetryConfig::checkMainThreadBusyRetry(const UnsafeStringView& path)
{
    return getOrCreateState(path).checkMainThreadBusyRetry();
}

bool BusyRetryConfig::checkHasBusyRetry(const UnsafeStringView& path)
{
    return getOrCreateState(path).checkHasBusyRetry();
}

bool BusyRetryConfig::onBusy(const UnsafeStringView& path, int numberOfTimes)
{
    WCDB_UNUSED(path);
    WCDB_UNUSED(numberOfTimes);

    Trying& trying = m_tryings.getOrCreate();
    WCTAssert(trying.valid());
    return getOrCreateState(trying.getPath()).wait(trying);
}

#pragma mark - State
BusyRetryConfig::Expecting::Expecting() : m_category(Category::None)
{
}

bool BusyRetryConfig::Expecting::valid() const
{
    return m_category == Category::Pager || m_category == Category::Shm;
}

void BusyRetryConfig::Expecting::expecting(ShmLockType type, int mask)
{
    m_category = Category::Shm;
    m_shmType = type;
    m_shmMask = mask;
}

void BusyRetryConfig::Expecting::expecting(PagerLockType type)
{
    m_category = Category::Pager;
    WCTAssert(type != PagerLockType::None);
    m_pagerType = type;
}

bool BusyRetryConfig::Expecting::satisfied(PagerLockType type) const
{
    WCTAssert(valid());
    bool satisfied = true;
    if (m_category == Category::Pager) {
        switch (m_pagerType) {
        case PagerLockType::Reserved:
            satisfied = type < PagerLockType::Reserved;
            break;
        case PagerLockType::Pending:
            // fallthrough
        case PagerLockType::Exclusive:
            satisfied = type == PagerLockType::None;
            break;
        case PagerLockType::Shared:
            satisfied = type < PagerLockType::Pending;
            break;
        default:
            WCTAssert(false);
            break;
        }
    }
    return satisfied;
}

bool BusyRetryConfig::Expecting::satisfied(int sharedMask, int exclusiveMask) const
{
    WCTAssert(valid());
    bool satisified = true;
    if (m_category == Category::Shm) {
        switch (m_shmType) {
        case ShmLockType::Shared:
            satisified = (m_shmMask & exclusiveMask) == 0;
            break;
        default:
            WCTAssert(m_shmType == ShmLockType::Exclusive);
            satisified
            = (m_shmMask & sharedMask) == 0 && (m_shmMask & exclusiveMask) == 0;
            break;
        }
    }
    return satisified;
}

BusyRetryConfig::State::ShmMask::ShmMask() : shared(0), exclusive(0)
{
}

BusyRetryConfig::State& BusyRetryConfig::getOrCreateState(const UnsafeStringView& path)
{
    WCTAssert(!path.empty());
    StateHolder& stateHoder = m_localState.getOrCreate();
    if (stateHoder.state != nullptr) {
        State& localState = *stateHoder.state;
        if (localState.m_path == path) {
            return localState;
        }
    }
    {
        SharedLockGuard lockGuard(m_statesLock);
        auto iter = m_states.find(path);
        if (iter != m_states.end()) {
            stateHoder.state = &iter->second;
            return *stateHoder.state;
        }
    }
    {
        LockGuard lockGuard(m_statesLock);
        State& state = m_states[path];
        state.m_path = path;
        stateHoder.state = &state;
        return state;
    }
}

BusyRetryConfig::State::State()
: m_pagerType(PagerLockType::None)
, m_localPagerType(PagerLockType::None)
, m_mainThreadBusyTrying(nullptr)
{
}

void BusyRetryConfig::State::updatePagerLock(PagerLockType type)
{
    std::lock_guard<std::mutex> lockGuard(m_lock);
    if (m_pagerType != type) {
        bool notify = type < m_pagerType;
        m_pagerType = type;
        PagerLockType& localPageType = m_localPagerType.getOrCreate();
        localPageType = type;
        if (notify) {
            tryNotify();
        }
    }
}

void BusyRetryConfig::State::updateShmLock(void* identifier, int sharedMask, int exclusiveMask)
{
    std::lock_guard<std::mutex> lockGuard(m_lock);
    bool notify = false;
    if (sharedMask == 0 && exclusiveMask == 0) {
        m_shmMasks.erase(identifier);
        m_localShmMask.getOrCreate().erase(identifier);
        notify = true;
    } else {
        State::ShmMask& mask = m_shmMasks[identifier];
        notify = sharedMask < mask.shared || exclusiveMask < mask.exclusive;
        mask.shared = sharedMask;
        mask.exclusive = exclusiveMask;
        State::ShmMask& localMask = m_localShmMask.getOrCreate()[identifier];
        localMask.shared = sharedMask;
        localMask.exclusive = exclusiveMask;
    }
    if (notify) {
        tryNotify();
    }
}

bool BusyRetryConfig::State::shouldWait(const Expecting& expecting) const
{
    bool wait = false;
    if (!expecting.satisfied(m_pagerType)) {
        wait = true;
    } else {
        for (const auto& iter : m_shmMasks) {
            if (!expecting.satisfied(iter.second.shared, iter.second.exclusive)) {
                wait = true;
                break;
            }
        }
    }
    return wait;
}

bool BusyRetryConfig::State::localShouldWait(const Expecting& expecting) const
{
    bool wait = false;
    if (!expecting.satisfied(m_localPagerType.getOrCreate())) {
        wait = true;
    } else {
        for (const auto& iter : m_localShmMask.getOrCreate()) {
            if (!expecting.satisfied(iter.second.shared, iter.second.exclusive)) {
                wait = true;
                break;
            }
        }
    }
    return wait;
}

bool BusyRetryConfig::State::wait(Trying& trying)
{
    static_assert(Exclusivity::Must < Exclusivity::NoMatter, "");

    std::unique_lock<std::mutex> lockGuard(m_lock);
    while (shouldWait(trying)) {
        Thread currentThread = Thread::current();
        // main thread first
        Exclusivity exclusivity
        = Thread::isMain() ? Exclusivity::Must : Exclusivity::NoMatter;

        m_waitings.insert(currentThread, trying, exclusivity);

        if (exclusivity == Exclusivity::Must) {
            m_mainThreadBusyTrying = &trying;
        }

        bool notified = m_conditional.wait_for(lockGuard, BusyRetryTimeOut);

        if (exclusivity == Exclusivity::Must) {
            m_mainThreadBusyTrying = nullptr;
        }

        m_waitings.erase(currentThread);

        if (!notified) {
            // retry anyway if timeout
            break;
        }
    }
    // never timeout
    return true;
}

bool BusyRetryConfig::State::checkMainThreadBusyRetry()
{
    if (m_mainThreadBusyTrying == nullptr) {
        return false;
    }
    std::unique_lock<std::mutex> lockGuard(m_lock);
    if (m_mainThreadBusyTrying == nullptr) {
        return false;
    }
    return localShouldWait(*m_mainThreadBusyTrying);
}

bool BusyRetryConfig::State::checkHasBusyRetry()
{
    std::unique_lock<std::mutex> lockGuard(m_lock);
    return m_waitings.size() > 0;
}

void BusyRetryConfig::State::tryNotify()
{
#ifdef __APPLE__
    for (auto iter = m_waitings.begin(); iter != m_waitings.end();) {
        if (shouldWait(iter->value())) {
            if (iter->order() == Exclusivity::Must) {
                // stop so that the main thread can hold the mutex first.
                return;
            }
            ++iter;
        } else {
            m_conditional.notify(iter->key());
            iter = m_waitings.erase(iter);
        }
    }
#else
    if (m_mainThreadBusyTrying == nullptr || !shouldWait(*m_mainThreadBusyTrying)) {
        m_conditional.notify_all();
    }
#endif
}

#pragma mark - Trying
void BusyRetryConfig::Trying::expecting(const UnsafeStringView& path, ShmLockType type, int mask)
{
    WCTAssert(!path.empty());
    m_path = path;
    Expecting::expecting(type, mask);
}

void BusyRetryConfig::Trying::expecting(const UnsafeStringView& path, PagerLockType type)
{
    WCTAssert(!path.empty());
    m_path = path;
    Expecting::expecting(type);
}

bool BusyRetryConfig::Trying::valid() const
{
    return !m_path.empty() && Expecting::valid();
}

const StringView& BusyRetryConfig::Trying::getPath() const
{
    return m_path;
}

#pragma mark - Lock Event
void BusyRetryConfig::willLock(const UnsafeStringView& path, PagerLockType type)
{
    m_tryings.getOrCreate().expecting(path, type);
}

void BusyRetryConfig::lockDidChange(const UnsafeStringView& path, PagerLockType type)
{
    getOrCreateState(path).updatePagerLock(type);
}

void BusyRetryConfig::willShmLock(const UnsafeStringView& path, ShmLockType type, int mask)
{
    m_tryings.getOrCreate().expecting(path, type, mask);
}

void BusyRetryConfig::shmLockDidChange(const UnsafeStringView& path,
                                       void* identifier,
                                       int sharedMask,
                                       int exclusiveMask)
{
    getOrCreateState(path).updateShmLock(identifier, sharedMask, exclusiveMask);
}

} // namespace WCDB
