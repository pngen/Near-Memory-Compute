# Contributing to Near-Memory Compute

Thank you for your interest in contributing. Contributions are welcome under the
terms of the Apache License, Version 2.0.

## Contribution terms

- All contributions are licensed under the Apache License, Version 2.0.
- No Contributor License Agreement (CLA) is required.
- No AI attribution is required, and no "Co-authored-by" trailers are expected
  or required. Please do not add them.
- Third-party attribution that is legally required must be preserved in the
  NOTICE file and in the relevant source files.

## How to contribute

1. Open an issue describing the change and why it is needed.
2. Fork the repository, create a topic branch, and make focused commits.
3. Ensure the build is clean (/W4 /WX) and the full test suite passes.
4. Keep the systems boundary narrow: this runtime decides whether execution
   should move toward data; it is not a Transfer Fabric, a generic accelerator
   scheduler, a compiler, or a general RPC runtime.
5. Submit a pull request with a clear description of the change and its
   validation.

## Release engineering

- Public releases use an annotated tag of the form vMAJOR.MINOR.PATCH.
- Post-release substantive fixes are released as the next appropriate patch
  release; do not rewrite or retag an existing release tag.
