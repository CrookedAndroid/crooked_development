// Copyright (C) 2023 The Android Open Source Project
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

use std::collections::HashMap;

use anyhow::{anyhow, Result};
use cfg_expr::{
    targets::{Arch, Family, Os},
    Predicate, TargetPredicate,
};
use crates_index::{http, Crate, Dependency, DependencyKind, SparseIndex, Version};
use reqwest::blocking::Client;
use semver::VersionReq;

pub struct CratesIoIndex {
    cache: HashMap<String, CratesIoCrate>,
    fetcher: Box<dyn CratesIoFetcher>,
}

pub trait CratesIoFetcher {
    fn fetch(&self, crate_name: &str) -> Result<CratesIoCrate>;
}
pub struct OnlineFetcher {
    index: SparseIndex,
    client: Client,
}
pub struct OfflineFetcher {
    index: SparseIndex,
}
impl CratesIoFetcher for OnlineFetcher {
    fn fetch(&self, crate_name: &str) -> Result<CratesIoCrate> {
        let req = self.index.make_cache_request(crate_name)?.body(())?;

        let (parts, _) = req.into_parts();
        let req = http::Request::from_parts(parts, vec![]);

        let req: reqwest::blocking::Request = req.try_into()?;

        let res = self.client.execute(req)?;

        let mut builder = http::Response::builder().status(res.status()).version(res.version());

        builder
            .headers_mut()
            .ok_or(anyhow!("Failed to get headers"))?
            .extend(res.headers().iter().map(|(k, v)| (k.clone(), v.clone())));

        let body = res.bytes().unwrap();
        let res = builder.body(body.to_vec()).unwrap();

        Ok(CratesIoCrate {
            krate: self
                .index
                .parse_cache_response(crate_name, res, true)?
                .ok_or(anyhow!("Crate not found"))?,
        })
    }
}
impl CratesIoFetcher for OfflineFetcher {
    fn fetch(&self, crate_name: &str) -> Result<CratesIoCrate> {
        Ok(CratesIoCrate { krate: self.index.crate_from_cache(crate_name.as_ref())? })
    }
}

impl CratesIoIndex {
    #[allow(dead_code)]
    pub fn new() -> Result<CratesIoIndex> {
        Ok(CratesIoIndex {
            cache: HashMap::new(),
            fetcher: Box::new(OnlineFetcher {
                index: crates_index::SparseIndex::new_cargo_default()?,
                client: reqwest::blocking::ClientBuilder::new().gzip(true).build()?,
            }),
        })
    }
    pub fn new_offline() -> Result<CratesIoIndex> {
        Ok(CratesIoIndex {
            cache: HashMap::new(),
            fetcher: Box::new(OfflineFetcher {
                index: crates_index::SparseIndex::new_cargo_default()?,
            }),
        })
    }
    pub fn get_crate(&mut self, crate_name: impl AsRef<str>) -> Result<&CratesIoCrate> {
        let crate_name = crate_name.as_ref();
        if !self.cache.contains_key(crate_name) {
            self.cache.insert(crate_name.to_string(), self.fetcher.fetch(crate_name)?);
        }
        self.cache.get(crate_name).ok_or(anyhow!("Crate not found"))
    }
}

pub struct CratesIoCrate {
    krate: Crate,
}

impl CratesIoCrate {
    pub fn versions<'a>(&'a self) -> impl DoubleEndedIterator<Item = &'a Version> {
        self.krate.versions().iter().filter(|v| {
            !v.is_yanked()
                && semver::Version::parse(v.version()).map_or(false, |parsed| parsed.pre.is_empty())
        })
    }
    pub fn versions_gt<'a>(
        &'a self,
        version: &'a semver::Version,
    ) -> impl DoubleEndedIterator<Item = &'a Version> {
        self.versions().filter(|v| {
            semver::Version::parse(v.version()).map_or(false, |parsed| parsed.gt(version))
        })
    }
    pub fn get_version(&self, version: &semver::Version) -> Option<&Version> {
        self.versions().find(|v| {
            semver::Version::parse(v.version()).map_or(false, |parsed| parsed.eq(version))
        })
    }
}

pub trait DependencyFilter {
    fn android_deps<'a>(&'a self) -> impl Iterator<Item = &'a Dependency>;
    fn android_deps_with_version_reqs<'a>(
        &'a self,
    ) -> impl Iterator<Item = (&'a Dependency, VersionReq)> {
        self.android_deps().filter_map(|dep| {
            VersionReq::parse(dep.requirement()).map_or(None, |req| Some((dep, req)))
        })
    }
}

impl DependencyFilter for Version {
    fn android_deps<'a>(&'a self) -> impl Iterator<Item = &'a Dependency> {
        // println!("Deps for {} {}", self.name(), self.version());
        self.dependencies().iter().filter(|dep| {
            // TODO: Take into account target as well.
            dep.kind() == DependencyKind::Normal && !dep.is_optional() && dep.is_android()
        })
    }
}

pub trait IsAndroid {
    fn is_android(&self) -> bool;
}
impl IsAndroid for Dependency {
    fn is_android(&self) -> bool {
        // println!("  {}", self.crate_name());
        self.target().map_or(true, is_android)
    }
}

pub trait NewDeps {
    fn is_new_dep(&self, base_deps: &HashMap<String, &str>) -> bool;
    fn is_changed_dep(&self, base_deps: &HashMap<String, &str>) -> bool;
}

impl NewDeps for Dependency {
    fn is_new_dep(&self, base_deps: &HashMap<String, &str>) -> bool {
        !base_deps.contains_key(self.crate_name())
    }

    fn is_changed_dep(&self, base_deps: &HashMap<String, &str>) -> bool {
        let base_dep = base_deps.get(self.crate_name());
        base_dep.is_none() || base_dep.is_some_and(|base_req| *base_req != self.requirement())
    }
}

fn is_android(target: &str) -> bool {
    // println!("    target = {}", target);
    let expr = cfg_expr::Expression::parse(target);
    if expr.is_err() {
        return false;
    }
    let expr = expr.unwrap();
    // println!("    {:?}", expr);
    expr.eval(|pred| match pred {
        Predicate::Target(target_predicate) => match target_predicate {
            TargetPredicate::Family(family) => *family == Family::unix,
            TargetPredicate::Os(os) => *os == Os::android || *os == Os::linux,
            TargetPredicate::Arch(arch) => {
                [Arch::arm, Arch::aarch64, Arch::riscv32, Arch::riscv64, Arch::x86, Arch::x86_64]
                    .contains(arch)
            }
            _ => true,
        },
        _ => true,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_android_cfgs() {
        assert!(!is_android("asmjs-unknown-emscripten"), "Parse error");

        assert!(!is_android("cfg(windows)"));
        assert!(is_android("cfg(unix)"));

        assert!(!is_android(r#"cfg(target_os = "redox")"#));

        assert!(!is_android(r#"cfg(target_arch = "wasm32")"#));
        assert!(is_android(r#"cfg(any(target_os = "linux", target_os = "android"))"#));
        assert!(is_android(
            r#"cfg(any(all(target_arch = "arm", target_pointer_width = "32"), target_arch = "mips", target_arch = "powerpc"))"#
        ));
        assert!(!is_android(
            r#"cfg(all(target_arch = "wasm32", target_vendor = "unknown", target_os = "unknown"))"#
        ));
        assert!(is_android("cfg(tracing_unstable)"));
        assert!(is_android(r#"cfg(any(unix, target_os = "wasi"))"#));
        assert!(is_android(r#"cfg(not(all(target_arch = "arm", target_os = "none")))"#))
    }
}
