# Contributing to Python DAP SDK

We love your input! We want to make contributing to Python DAP SDK as easy and transparent as possible, whether it's:

- Reporting a bug
- Discussing the current state of the code
- Submitting a fix
- Proposing new features
- Becoming a maintainer

## We Develop with GitLab

We use GitLab to host code, to track issues and feature requests, as well as accept merge requests.

## We Use [GitLab Flow](https://docs.gitlab.com/ee/topics/gitlab_flow.html)

Pull requests are the best way to propose changes to the codebase. We actively welcome your merge requests:

1. Fork the repo and create your branch from `master`.
2. If you've added code that should be tested, add tests.
3. If you've changed APIs, update the documentation.
4. Ensure the test suite passes.
5. Make sure your code lints.
6. Issue that merge request!

## Any contributions you make will be under the GNU AGPL-3.0 License

In short, when you submit code changes, your submissions are understood to be under the same [GNU AGPL-3.0 License](LICENSE) that covers the project. Feel free to contact the maintainers if that's a concern.

## Report bugs using GitLab's [issue tracker](https://gitlab.demlabs.net/dap/python-dap/-/issues)

We use GitLab issues to track public bugs. Report a bug by [opening a new issue](https://gitlab.demlabs.net/dap/python-dap/-/issues/new); it's that easy!

## Write bug reports with detail, background, and sample code

**Great Bug Reports** tend to have:

- A quick summary and/or background
- Steps to reproduce
  - Be specific!
  - Give sample code if you can.
- What you expected would happen
- What actually happens
- Notes (possibly including why you think this might be happening, or stuff you tried that didn't work)

## Development Process

1. Clone the repository:
   ```bash
   git clone https://gitlab.demlabs.net/dap/python-dap
   cd python-dap
   ```

2. Install development dependencies:
   ```bash
   pip install -e ".[dev]"
   ```

3. Create a new branch:
   ```bash
   git checkout -b feature-xyz
   ```

4. Make your changes and add tests.

5. Run the test suite:
   ```bash
   pytest
   ```

6. Run the type checker:
   ```bash
   mypy dap
   ```

7. Format your code:
   ```bash
   black dap
   isort dap
   ```

8. Commit your changes:
   ```bash
   git add .
   git commit -m "Add feature xyz"
   ```

9. Push to your fork and submit a merge request.

## Testing

We use pytest for testing. All tests are in the `tests/` directory:

- `tests/unit/` - Unit tests
- `tests/integration/` - Integration tests

To run tests:
```bash
# Run all tests
pytest

# Run with coverage
pytest --cov=dap

# Run specific test file
pytest tests/unit/test_crypto.py

# Run specific test
pytest tests/unit/test_crypto.py::test_key_creation
```

## Code Style

We use several tools to maintain code quality:

- [Black](https://black.readthedocs.io/) for code formatting
- [isort](https://pycqa.github.io/isort/) for import sorting
- [mypy](http://mypy-lang.org/) for static type checking
- [pylint](https://www.pylint.org/) for code analysis

Configuration for these tools is in `pyproject.toml`.

## Documentation

- Use docstrings for all public modules, functions, classes, and methods.
- Follow [Google style](https://google.github.io/styleguide/pyguide.html#38-comments-and-docstrings) for docstrings.
- Keep the [README.md](README.md) up to date.

## License

By contributing, you agree that your contributions will be licensed under its GNU AGPL-3.0 License. 