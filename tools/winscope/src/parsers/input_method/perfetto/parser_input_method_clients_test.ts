/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
import {assertDefined} from 'common/assert_utils';
import {TimestampConverterUtils} from 'test/unit/timestamp_converter_utils';
import {UnitTestUtils} from 'test/unit/utils';
import {CoarseVersion} from 'trace/coarse_version';
import {Parser} from 'trace/parser';
import {TraceType} from 'trace/trace_type';
import {HierarchyTreeNode} from 'trace/tree_node/hierarchy_tree_node';

describe('Perfetto ParserInputMethodClients', () => {
  let parser: Parser<HierarchyTreeNode>;

  beforeAll(async () => {
    jasmine.addCustomEqualityTester(UnitTestUtils.timestampEqualityTester);
    parser = (await UnitTestUtils.getPerfettoParser(
      TraceType.INPUT_METHOD_CLIENTS,
      'traces/perfetto/ime.perfetto-trace',
    )) as Parser<HierarchyTreeNode>;
  });

  it('has expected trace type', () => {
    expect(parser.getTraceType()).toEqual(TraceType.INPUT_METHOD_CLIENTS);
  });

  it('has expected coarse version', () => {
    expect(parser.getCoarseVersion()).toEqual(CoarseVersion.LATEST);
  });

  it('provides timestamps', () => {
    expect(assertDefined(parser.getTimestamps()).length).toEqual(56);

    const expected = [
      TimestampConverterUtils.makeRealTimestamp(1714659585862265133n),
      TimestampConverterUtils.makeRealTimestamp(1714659585890068600n),
      TimestampConverterUtils.makeRealTimestamp(1714659587314072751n),
    ];
    expect(assertDefined(parser.getTimestamps()).slice(0, 3)).toEqual(expected);
  });

  it('retrieves trace entry', async () => {
    const entry = await parser.getEntry(1);
    expect(entry).toBeInstanceOf(HierarchyTreeNode);
    expect(entry.id).toEqual('InputMethodClients entry');
  });

  it('translates intdefs', async () => {
    const entry = await parser.getEntry(7);
    const client = assertDefined(entry.getChildByName('client'));
    const properties = await client.getAllProperties();
    const intdefProperty = assertDefined(
      properties
        ?.getChildByName('viewRootImpl')
        ?.getChildByName('windowAttributes')
        ?.getChildByName('type'),
    );
    expect(intdefProperty.formattedValue()).toEqual('TYPE_BASE_APPLICATION');
  });
});