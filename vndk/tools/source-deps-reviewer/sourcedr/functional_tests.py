#!/usr/bin/env python3

from sourcedr import data_utils
from sourcedr.data_utils import data_path, load_data, remove_data
from sourcedr.map import (
    load_build_dep_file_from_path, load_review_data,
    link_build_dep_and_review_data,
)
from sourcedr.preprocess import CodeSearch
from sourcedr.server import app, args

from flask import Flask, jsonify, render_template, request
from flask_testing import LiveServerTestCase, TestCase
from urllib.request import urlopen

import flask_testing
import json
import os
import unittest

app.config['TESTING'] = True

ANDROID_ROOT = 'sourcedr/test'

class TestPreprocess(unittest.TestCase):
    def test_prepare(self):
        remove_data()
        engine = CodeSearch.create_default(android_root=ANDROID_ROOT)
        engine.build_index()
        engine.find(patterns=['dlopen'], is_regexs=[False])
        self.assertTrue(os.path.exists(data_path))

class TestViews(TestCase):
    def create_app(self):
        # TODO: This refers to `sourcedr.server.args`.  This should be removed
        # in the upcoming refactor process.
        args.android_root = ANDROID_ROOT
        return app

    def setUp(self):
        engine = CodeSearch.create_default(android_root=ANDROID_ROOT)
        engine.build_index()
        engine.find(patterns=['dlopen'], is_regexs=[False])

    def tearDown(self):
        remove_data()

    def test_get_file(self):
        test_arg = 'example.c'
        response = self.client.get('/get_file',
                                   query_string=dict(path=test_arg))
        ret = response.json['result']
        with open(os.path.join(ANDROID_ROOT, test_arg), 'r') as f:
            self.assertEqual(ret, f.read())

    def test_load_file(self):
        test_arg = 'dlopen/test.c'
        test_arg += ':10:    handle = dlopen("libm.so.6", RTLD_LAZY);'
        response = self.client.get('/load_file',
                                   query_string=dict(path=test_arg))
        deps = json.loads(response.json['deps'])
        codes = json.loads(response.json['codes'])
        with open(data_path, 'r') as f:
            cdata = json.load(f)

        self.assertEqual(deps, cdata[test_arg][0])
        self.assertEqual(codes, cdata[test_arg][1])

    def test_save_all(self):
        label = os.path.abspath('sourcedr/test/dlopen/test.c')
        label += ':10:    handle = dlopen("libm.so.6", RTLD_LAZY);'
        test_arg = {
            'label': label,
            'deps': json.dumps(['this_is_a_test.so']),
            'codes': json.dumps(['arr_0', 'arr_1'])
        }
        response = self.client.get('/save_all', query_string=test_arg)
        cdata = load_data()
        self.assertEqual(['this_is_a_test.so'],  cdata[test_arg['label']][0])
        self.assertEqual(['arr_0', 'arr_1'], cdata[test_arg['label']][1])


class MapTest(unittest.TestCase):
    def setUp(self):
        # TODO: Remove this global variable hacks after refactoring process.
        self.old_data_path = data_utils.data_path
        data_utils.data_path = 'sourcedr/test/map/data.json'

    def tearDown(self):
        # TODO: Remove this global variable hacks after refactoring process.
        data_utils.data_path = self.old_data_path

    def test_load_build_dep_file(self):
        dep = load_build_dep_file_from_path('sourcedr/test/map/build_dep.json')

        self.assertIn('liba.so', dep)
        self.assertIn('libb.so', dep)
        self.assertIn('libc.so', dep)

        self.assertSetEqual({'a.h', 'a1.c', 'a1.o', 'a2.c', 'a2.o'}, dep['liba.so'])
        self.assertSetEqual({'a.h', 'b.c', 'b.o'}, dep['libb.so'])
        self.assertSetEqual(set(), dep['libc.so'])

    def test_load_review_data(self):
        data = load_review_data()
        self.assertIn('a.h', data)
        self.assertEqual(['libx.so'], data['a.h'])

    def test_link_build_dep_and_review_data(self):
        dep = load_build_dep_file_from_path('sourcedr/test/map/build_dep.json')
        data = load_review_data()
        result = link_build_dep_and_review_data(dep, data)

        self.assertIn('liba.so', result)
        self.assertIn('libb.so', result)
        self.assertIn('libc.so', result)

        self.assertEqual(['libx.so'], result['liba.so'])
        self.assertEqual(['libx.so'], result['libb.so'])


if __name__ == '__main__':
    unittest.main()
