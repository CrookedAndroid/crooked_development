use anyhow::Result;
use crates_index::{http, SparseIndex};

fn main() -> Result<()> {
    let crate_name = std::env::args().nth(1).expect("Need to specify crate name");
    let mut index = crates_index::SparseIndex::new_cargo_default()?;
    update(&mut index, &crate_name);
    print_crate(&mut index, &crate_name)?;

    Ok(())
}

// To select versions:
// * not yanked
// * not pre-release (parsed version does not have "pre")

// Dependencies:
// * Can be Normal, Dev, or Build
// * Specify a version requirement.
// * May or may not have default features.
// * May be optional, in which case it depends on enabling a feature in the crate.
// * Can depend on the target arch.
// * Probably more stuff as well.

fn print_crate(index: &mut SparseIndex, crate_name: impl AsRef<str>) -> Result<()> {
    let crate_name = crate_name.as_ref();
    let krate = index.crate_from_cache(crate_name)?;
    println!("{:?}", krate.highest_normal_version().unwrap().version());
    for version in krate.versions() {
        let parsed = semver::Version::parse(version.version())?;
        println!("{}, {}, {}", version.version(), version.is_yanked(), parsed.pre.is_empty());
        for dep in version.dependencies() {
            println!(
                "  {} {} {:?} {} {}, {}",
                dep.crate_name(),
                dep.requirement(),
                dep.kind(),
                dep.has_default_features(),
                dep.is_optional(),
                dep.target().unwrap_or("all arch")
            );
        }
    }
    Ok(())
}

fn update(index: &mut SparseIndex, crate_name: impl AsRef<str>) {
    let crate_name = crate_name.as_ref();
    let req = index.make_cache_request(crate_name).unwrap().body(()).unwrap();

    let (parts, _) = req.into_parts();
    let req = http::Request::from_parts(parts, vec![]);

    let req: reqwest::blocking::Request = req.try_into().unwrap();

    let client = reqwest::blocking::ClientBuilder::new().gzip(true).build().unwrap();

    let res = client.execute(req).unwrap();

    let mut builder = http::Response::builder().status(res.status()).version(res.version());

    builder
        .headers_mut()
        .unwrap()
        .extend(res.headers().iter().map(|(k, v)| (k.clone(), v.clone())));

    let body = res.bytes().unwrap();
    let res = builder.body(body.to_vec()).unwrap();

    index.parse_cache_response(crate_name, res, true).unwrap();
}
