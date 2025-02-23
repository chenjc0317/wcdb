//
// Created by 陈秋文 on 2022/8/13.
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

#import "CPPTestCaseObject.h"
#import "Random.h"
#import <WCDB/Value.hpp>

@interface Random (CPPTestObject)

- (WCDB::MultiRowsValue)testCaseValuesWithCount:(NSUInteger)count startingFromIdentifier:(int64_t)identifier;

- (WCDB::MultiRowsValue)autoIncrementTestCaseValuesWithCount:(NSUInteger)count;

- (CPPTestCaseObject)testCaseObjectWithIdentifier:(int)identifier;

- (CPPTestCaseObject)autoIncrementTestCaseObject;

- (CPPTestCaseObject)autoIncrementTestCaseObjectWithIdentifier:(int)identifier;

- (WCDB::ValueArray<CPPTestCaseObject>)testCaseObjectsWithCount:(NSUInteger)count startingFromIdentifier:(int)identifier;

- (WCDB::ValueArray<CPPTestCaseObject>)autoIncrementTestCaseObjectsWithCount:(NSUInteger)count;

@end
