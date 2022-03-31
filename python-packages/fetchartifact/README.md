# fetchartifact

This is a Python interface to http://go/fetchartifact, which is used for
fetching artifacts from http://go/ab.

## Usage

```python
from fetchartifact import fetchartifact


async def main() -> None:
    artifacts = await fetch_artifact(
        branch="aosp-master-ndk",
        target="linux",
        build="1234",
        pattern="android-ndk-*.zip",
    )
    for artifact in artifacts:
        print(f"Downloaded {artifact}")
```

## Development

For first time set-up, install https://python-poetry.org/, then run
`poetry install` to install the project's dependencies.

This project uses mypy and pylint for linting, black and isort for
auto-formatting, and pytest for testing. All of these tools will be installed
automatically, but you may want to configure editor integration for them.

To run any of the tools poetry installed, you can either prefix all your
commands with `poetry run` (as in `poetry run pytest`), or you can run
`poetry shell` to enter a shell with all the tools on the `PATH`. The following
instructions assume you've run `poetry shell` first.

To run the linters:

```bash
mypy fetchartifact tests
pylint fetchartifact tests
```

To auto-format the code (though I recommend configuring your editor to do this
on save):

```bash
isort .
black .
```

To run the tests and generate coverage:

```bash
coverage run -m pytest
coverage report
```
