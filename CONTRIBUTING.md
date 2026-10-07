# Contributing

Thank you for helping. A few things to know before opening a pull request:

- **Target `develop`.** Work is integrated on the `develop` branch; `main` only receives releases
  (at a feature freeze `develop` is merged into it and tagged). Make your branch from `develop` and
  open the pull request against `develop`. Fixes for a published release are the exception: they
  are made from `main` and merged back into `develop` by the maintainers.
- **No game content.** Never commit anything taken from the game or derived from its files:
  assets, executables, extracted data, captures, saves, logs of runs, or text copied from the
  game or from its translations. The repository and its releases contain no game content, and pull
  requests that add any are closed.
- **Keep changes generic.** Behavior is decided by capability or state, never by a hash, a mesh or
  material name, or another identifier of particular game content.
- **English** for code, comments, documentation and commit messages.
- **Tests.** Build and run the tests as described in [docs/BUILDING.md](docs/BUILDING.md); CI runs
  them on Linux and Windows for every pull request. Say how you checked your change.
- **Licenses.** The project's code is GPL-3.0 ([LICENSE](LICENSE)); changes to the ReXGlue SDK go
  in [patches/](patches/) under BSD-3-Clause ([patches/LICENSE](patches/LICENSE)) so they can go
  upstream. By contributing you agree that your contribution is licensed under the same terms.

How the port is organized: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
