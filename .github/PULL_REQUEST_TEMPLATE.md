<!-- Thanks for contributing! Keep PRs focused. See CONTRIBUTING.md. -->

## What & why
Briefly: what does this change and why?

## How verified
- [ ] `ctest` passes locally
- [ ] Added/updated a deterministic test (Null backend) for the change
- [ ] `dart analyze` + `flutter test` (if Dart/Flutter touched)
- [ ] C# bindings build + smoke (if C ABI / C# touched)

## Checklist
- [ ] No allocations / locks / non-RT-safe calls added to the audio-thread path
- [ ] C ABI change (if any) is **additive**, bumps `ROPE_ABI_VERSION_MINOR`, and
      is mirrored in the Dart and C# bindings (see docs/ABI_POLICY.md)
- [ ] Code, comments, and commit messages in English
- [ ] No build artifacts committed (`build*/`, `stage/`, `bin/`, `obj/`)
