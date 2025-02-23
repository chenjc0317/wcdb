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

#import "VirtualTableFTS5Object.h"
#import "VirtualTableFTS5Object+WCTTableCoding.h"
#import <WCDB/WCDB.h>

@implementation VirtualTableFTS5Object

WCDB_IMPLEMENTATION(VirtualTableFTS5Object)
WCDB_SYNTHESIZE(identifier)
WCDB_SYNTHESIZE(content)

WCDB_VIRTUAL_TABLE_MODULE(WCTModuleFTS5)

WCDB_VIRTUAL_TABLE_TOKENIZE(WCTTokenizerPorter)

WCDB_VIRTUAL_TABLE_EXTERNAL_CONTENT("contentTable")

WCDB_UNINDEXED(identifier)

@end
