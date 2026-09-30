# Contributing

## Apps

An app is one directory under `src/apps/` and changes no file outside it. Open
a pull request with the directory and, if it has hardware-free logic worth
checking, a `test/test_<name>/` beside it that passes on `pio test -e native`.

Read [docs/writing-apps.md](docs/writing-apps.md) first. The two things
reviewers ask for most: declare the hardware you need in `AppInfo::needs`
rather than reaching for it, and keep the maths in a file that compiles
without Arduino so it can be tested.

## The base

`src/core/`, `src/ui/`, `src/services/`, `src/mathx/` and `src/web/` are
generated from a private tree by an export, so a commit made to them here is
replaced by the next release.

Send base changes as a pull request anyway. They are applied upstream and come
back in the next export with you kept as the commit's author. Nothing under
`src/apps/` is touched by an export, so app work is never overwritten.
