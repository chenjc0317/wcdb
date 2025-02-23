//
// Created by sanhuazhang on 2018/05/31
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

#include <WCDB/Assemble.hpp>
#include <WCDB/Assertion.hpp>
#include <WCDB/FullCrawler.hpp>
#include <WCDB/MasterItem.hpp>
#include <WCDB/Page.hpp>
#include <WCDB/SequenceCrawler.hpp>
#include <WCDB/SequenceItem.hpp>
#include <WCDB/StringView.hpp>
#include <WCDB/SyntaxCommonConst.hpp>

namespace WCDB {

namespace Repair {

#pragma mark - Initialize
FullCrawler::FullCrawler(const UnsafeStringView &source)
: Repairman(source), m_masterCrawler(), m_sequenceCrawler()
{
    m_sequenceCrawler.setAssociatedPager(&m_pager);
    m_masterCrawler.setAssociatedPager(&m_pager);
}

FullCrawler::~FullCrawler() = default;

#pragma mark - Repair
bool FullCrawler::work()
{
    auto isEmpty = isEmptyDatabase();
    if (isEmpty.succeed()) {
        if (isEmpty.value()) {
            return exit(true);
        }
    } else {
        return exit(false);
    }

    if (m_cipherDelegate->isCipherDB()) {
        size_t pageSize = m_cipherDelegate->getCipherPageSize();
        if (pageSize == 0) {
            setCriticalError(m_cipherDelegate->getCipherError());
            return exit(false);
        }
        void *pCodec = m_cipherDelegate->getCipherContext();
        if (pCodec == nullptr) {
            setCriticalError(m_cipherDelegate->getCipherError());
            return exit(false);
        }
        m_pager.setCipherContext(m_cipherDelegate->getCipherContext());
        m_pager.setPageSize((int) pageSize);
    }

    if (!m_pager.initialize()) {
        if (m_pager.getError().isCorruption()) {
            tryUpgradeCrawlerError();
            return exit(true);
        } else {
            setCriticalError(m_pager.getError());
            return exit(false);
        }
    }

    //calculate score
    int numbersOfLeafTablePages = 0;
    for (int i = 1; i <= m_pager.getNumberOfPages(); ++i) {
        Page page(i, &m_pager);
        auto type = page.acquireType();
        // treat as leaf table if unknown
        if (type.failed() || type.value() == Page::Type::LeafTable) {
            ++numbersOfLeafTablePages;
        }
    }
    if (numbersOfLeafTablePages == 0) {
        return exit(true);
    }
    setPageWeight(Fraction(1, numbersOfLeafTablePages));

    if (markAsAssembling()) {
        m_masterCrawler.work(this);
        for (const auto &element : m_associatedSQLs) {
            assembleAssociatedSQLs(element.second);
        }
        markAsAssembled();
    }

    return exit();
}

#pragma mark - Crawlable
void FullCrawler::onCellCrawled(const Cell &cell)
{
    if (isErrorCritial()) {
        return;
    }
    assembleCell(cell);
}

bool FullCrawler::willCrawlPage(const Page &page, int)
{
    if (isErrorCritial()) {
        return false;
    }
    markPageAsCounted(page);
    increaseProgress(getPageWeight().value());
    return true;
}

#pragma mark - Filter
void FullCrawler::filter(const Filter &tableShouldBeCrawled)
{
    m_filter = tableShouldBeCrawled;
}

bool FullCrawler::filter(const UnsafeStringView &tableName)
{
    bool result = true;
    if (m_filter != nullptr) {
        result = m_filter(tableName);
    }
    return result;
}

#pragma mark - Error
void FullCrawler::onErrorCritical()
{
    m_masterCrawler.suspend();
    m_sequenceCrawler.suspend();
    Repairman::onErrorCritical();
}

#pragma mark - MasterCrawlerDelegate
void FullCrawler::onMasterPageCrawled(const Page &page)
{
    increaseProgress(getPageWeight().value());
    markPageAsCounted(page);
}

void FullCrawler::onMasterCellCrawled(const Cell &cell, const MasterItem &master)
{
    if (isErrorCritial()) {
        return;
    }
    markCellAsCounted(cell);
    if (master.name == Syntax::sequenceTable) {
        WCTAssert(master.type.caseInsensiveEqual("table"));
        WCTAssert(master.tableName.caseInsensiveEqual(master.name));
        m_sequenceCrawler.work(master.rootpage, this);
    } else if (!filter(master.tableName)
               || MasterItem::isReservedTableName(master.name)
               || MasterItem::isReservedTableName(master.tableName)) {
        //Skip no backup table and sqlite reserved table
        return;
    } else {
        if (master.type.caseInsensiveEqual("table")) {
            WCTAssert(master.tableName.caseInsensiveEqual(master.name));
            if (assembleTable(master.name, master.sql)) {
                crawl(master.rootpage);
            }
        } else {
            if (!master.sql.empty()) {
                m_associatedSQLs[master.tableName].push_back(master.sql);
            }
        }
    }
}

void FullCrawler::onMasterCrawlerError()
{
    tryUpgradeCrawlerError();
}

#pragma mark - SequenceCrawlerDelegate
void FullCrawler::onSequencePageCrawled(const Page &page)
{
    if (isErrorCritial()) {
        return;
    }
    increaseProgress(getPageWeight().value());
    markPageAsCounted(page);
}

void FullCrawler::onSequenceCellCrawled(const Cell &cell, const SequenceItem &sequence)
{
    if (isErrorCritial()) {
        return;
    }
    if (assembleSequence(sequence.name, sequence.seq)) {
        markCellAsCounted(cell);
    }
}

void FullCrawler::onSequenceCrawlerError()
{
    tryUpgradeCrawlerError();
}

} //namespace Repair

} //namespace WCDB
