// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use std::{
    collections::BTreeSet,
    path::{Path, PathBuf},
};

use anyhow::{anyhow, Result};
use glob::glob;

static LICENSE_GLOBS: &'static [&'static str] =
    &["LICENSE", "LICENCE", "LICENSE.*", "LICENSE-*", "LICENSES/*", "UNLICENSE", "COPYING"];

pub(crate) fn find_license_files(path: impl AsRef<Path>) -> Result<BTreeSet<PathBuf>> {
    multiglob(path, LICENSE_GLOBS.iter())
}

fn multiglob<T: AsRef<str>>(
    path: impl AsRef<Path>,
    patterns: impl Iterator<Item = T>,
) -> Result<BTreeSet<PathBuf>> {
    let path = path.as_ref();
    let mut matches = BTreeSet::new();
    for pattern in patterns {
        let pattern = pattern.as_ref();
        for file in
            glob(&path.join(pattern).to_str().ok_or(anyhow!("Failed to convert path to string"))?)?
        {
            let file = file?;
            if !file.is_symlink() {
                matches.insert(file.strip_prefix(path)?.to_owned());
            }
        }
    }
    Ok(matches)
}
