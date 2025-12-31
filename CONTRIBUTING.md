
# DL Streamer Contributor Guide

The following are guidelines for contributing to the DL Streamer project, including the code of conduct, submitting issues, and contributing code.

# Table of Contents

- [`Code of Conduct`](#code-of-conduct)
- [`Security`](#security)
- [`Get Started`](#get-started)
- [`How to Contribute`](#how-to-contribute)
- [`Development Guidelines`](#development-guidelines)
- [`Sign Your Work`](#sign-your-work)
- [`License`](#license)

# Code of Conduct

This project and everyone participating in it are governed by the [`CODE_OF_CONDUCT`](CODE_OF_CONDUCT.md) document. By participating, you are expected to adhere to this code.

# Security

Read the [`Security Policy`](SECURITY.md).

# Get Started

Clone the repository and follow the [`README`](README.md) to get started with the sample applications of interest.

```
    git clone https://github.com/open-edge-platform/dlstreamer.git
    cd dlstreamer
```

# How to Contribute

## Contribute Code Changes

> If you want to help improve DL Streamer, choose one of the issues reported in [`GitHub Issues`](issues) and create a [`Pull Request`](pulls) to address it.
> Note: Please check that the change hasn't been implemented before you start working on it.

## Improve Documentation

The easiest way to help with the `Developer Guide` and `User Guide` is to review it and provide feedback on the
existing articles. Whether you notice a mistake, see the possibility of improving the text, or think more
information should be added, you can reach out to discuss the potential changes.

## Report Bugs

If you encounter a bug, open an issue in [`Github Issues`](issues). Provide the following information to help us
understand and resolve the issue quickly:

- A clear and descriptive title
- A thorough description of the issue
- Steps to reproduce the issue
- Expected versus actual behavior
- Screenshots or logs (if applicable)
- Your environment (OS, browser, etc.)

## Suggest Enhancements

Intel welcomes suggestions for new features and improvements. Follow these steps to make a suggestion:

- Check if there's already a similar suggestion in [`Github Issues`](issues).
- If not, open a new issue and provide the following information:
   - A clear and descriptive title
   - A detailed description of the enhancement
   - Use cases and benefits
   - Any additional context or references

## Submit Pull Requests

Before submitting a pull request, ensure you follow these guidelines:

- Fork the repository and create your branch from `main`.
- Follow the [`Development Guidelines`](#development-guidelines) in this document.
- Test your changes thoroughly.
- Document your changes (in code, readme, etc.).
- Submit your pull request, detailing the changes and linking to any relevant issues.
- Wait for a review. Intel will review your pull request as soon as possible and provide you with feedback.
You can expect a merge once your changes are validated with automatic tests and approved by maintainers.

# Development Guidelines

## Coding Standards

Consistently following coding standards helps maintain readability and quality. Adhere to the following conventions:
- Language-specific style guides
- Properly formatted code with tools like `Prettier` and `ESLint`
- Meaningful variable and function names
- Inline comments and API documentation generator for complex logic

## Commit Messages

Clear and informative commit messages make it easier to understand the history of the project. Follow these guidelines:
- Use the present tense (e.g., "Add feature" not "Added feature")
- Capitalize the first letter
- Keep the message concise, ideally under 50 characters

Use the following format in your Pull Request messages:

```
### Description

Please include a summary of the changes and the related issue. List any dependencies that are required for this change.

Fixes # (issue)

### Any Newly Introduced Dependencies

Please describe any newly introduced 3rd party dependencies in this change. List their name, license information and how they are used in the project.

### How Has This Been Tested?

Please describe the tests that you ran to verify your changes. Provide instructions so we can reproduce. Please also list any relevant details for your test configuration

### Checklist:

- [ ] I agree to use the MIT license for my code changes
- [ ] I have not introduced any 3rd party dependency changes
- [ ] I have performed a self-review of my code
```

## Testing

Thorough testing is crucial to maintain project stability. Ensure that you:
- Write unit tests for new and existing code
- Use testing frameworks and tools
- Run tests locally before submitting a pull request
- Check for code coverage and aim for high coverage percentage

# Sign Your Work

Please use the sign-off line at the end of the patch. Your signature certifies that you wrote the patch or otherwise have the right to pass it on as an open-source patch. The rules are pretty simple: if you can certify the below (from developercertificate.org):

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.
660 York Street, Suite 102,
San Francisco, CA 94110 USA

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.

Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

Then you just add a line to every git commit message:

```
Signed-off-by: Your Name <your.name@email.com>
```

Use your real name (sorry, no pseudonyms or anonymous contributions.)

If you set your `user.name` and `user.email` with `git`, you can sign your commit automatically with `git commit -s`.

# License

By contributing to this project, you agree that your contributions will be licensed under the [`MIT](LICENSE) LICENSE of the repository.

