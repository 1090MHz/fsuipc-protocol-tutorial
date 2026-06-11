# Contributing to FS6IPC Protocol Tutorial

Thank you for your interest in contributing! This project aims to be **the definitive educational resource** for understanding the FS6IPC protocol used by all FSUIPC versions (1-7).

> **Project Focus:** This is an educational protocol implementation, not a replacement for the official FSUIPC product. We're teaching how the protocol works.

## 🎯 Project Mission

Make the FS6IPC protocol **accessible and understandable** for developers building:

- FSUIPC-compatible applications
- Flight simulator integration tools
- Testing frameworks

## 🤝 How to Contribute

### Documentation Improvements

Documentation is as important as code! Help by:

- **Clarifying tutorials** - Found something confusing? Submit a PR with clearer wording
- **Adding examples** - Real-world code snippets showing how to use offsets
- **Fixing typos** - Even small fixes are appreciated
- **Translating** - Help make this accessible to non-English speakers

**How:**

1. Fork the repository
2. Edit markdown files
3. Submit a pull request with a clear description of what you improved

### Code Contributions

#### New Offset Implementations

Add support for more FSUIPC offsets:

1. **Choose an offset** from the [official list](http://fsuipcoffsets.com/)
2. **Implement in server** (`fsuipc_server.cpp`):
   ```cpp
   // In FlushSimState():
   uint32_t new_offset_raw = EncodeNewOffset(state.new_value);
   WriteOff(0xXXXX, new_offset_raw);
   ```
3. **Add decoding function** with examples
4. **Update documentation** (`OFFSETS.md`)
5. **Test with client**

#### Performance Optimizations

- Reduce mutex contention
- Optimize encoding/decoding functions
- Profile and identify bottlenecks

#### Bug Fixes

Found a bug? Please:

1. **Open an issue first** describing the problem
2. **Write a test** that reproduces it
3. **Fix the bug**
4. **Verify the test passes**
5. **Submit a PR**

### Testing

Help ensure compatibility:

- **Test with FSUIPC apps** - Try popular add-ons and report results
- **Different Windows versions** - Verify on Win10, Win11
- **Edge cases** - What happens with extreme values?
- **Performance testing** - How many IPC calls per second?

## 📋 Contribution Guidelines

### Code Style

**C++ Formatting:**

```cpp
// Use descriptive names
double DecodeHeading(uint32_t raw) {  // Good
double Decode(uint32_t x) {           // Bad

// Comments explain WHY, not WHAT
// Convert from FSUIPC's fixed-point format
double heading = raw * 360.0 / 65536.0;

// Use const where appropriate
const uint32_t HEADING_OFFSET = 0x0238;

// Prefer C++ casts over C-style
auto* pkt = reinterpret_cast<IPC_ReadPacket*>(ptr);  // Good
auto* pkt = (IPC_ReadPacket*)ptr;                    // Avoid
```

**Error Handling:**

```cpp
// Always check for errors
HANDLE hMap = CreateFileMapping(...);
if (!hMap) {
    std::cerr << "CreateFileMapping failed: " << GetLastError() << "\n";
    return false;
}
```

**Resource Management:**

```cpp
// RAII: cleanup in destructors
// Avoid manual CloseHandle everywhere
```

### Commit Messages

Use clear, descriptive commit messages:

✅ **Good:**

```
Add support for rudder position offset (0x0BC0)

- Implement encoding in SimState
- Add DecodeRudder() function
- Update OFFSETS.md with examples
- Test with client application
```

❌ **Bad:**

```
Update code
```

### Pull Request Process

1. **Create a branch** from `main`:

   ```bash
   git checkout -b feature/add-rudder-offset
   ```

2. **Make your changes** with clear commits

3. **Test thoroughly:**

   ```bash
   cmake --build build --config Release
   .\build\Release\fsuipc_server.exe  # In one terminal
   .\build\Release\fsuipc_test_client.exe  # In another
   ```

4. **Update documentation** if needed

5. **Submit PR** with:
   - **Clear title**: "Add rudder position offset support"
   - **Description**: What you changed and why
   - **Testing**: How you verified it works
   - **Related issues**: Link any relevant issues

6. **Respond to review** - Be open to feedback!

### Documentation Style

**Markdown Guidelines:**

```markdown
## Use Clear Headings

### Be Specific

Explain concepts step-by-step:

1. What the feature does
2. How it works internally
3. Example code
4. Common pitfalls

Use code blocks with syntax highlighting:

\`\`\`cpp
double heading = DecodeHeading(raw);
\`\`\`

Add tables for reference data:

| Offset | Size | Type  |
| ------ | ---- | ----- |
| 0x0238 | 4    | DWORD |
```

## 🐛 Reporting Bugs

### Good Bug Reports Include:

1. **Description** - What went wrong?
2. **Steps to reproduce** - Exact steps to trigger the bug
3. **Expected behavior** - What should happen?
4. **Actual behavior** - What actually happened?
5. **Environment**:
   - Windows version
   - Compiler (MSVC/GCC/Clang)
   - CMake version
6. **Error messages** - Full error output
7. **Code sample** - Minimal code that reproduces issue

### Example Bug Report:

```
## Server crashes when client disconnects during IPC call

### Steps to Reproduce
1. Start fsuipc_server.exe
2. Start fsuipc_test_client.exe
3. Kill client process during "Processing..." message

### Expected
Server handles disconnect gracefully

### Actual
Server crashes with access violation

### Environment
- Windows 11 23H2
- MSVC 19.38
- CMake 3.27

### Error Output
```

Exception at 0x00007FF123456789: Access violation reading location 0x0000000000000000

```

### Possible Cause
Null pointer dereference in HandleIPCMessage() when client mapping disappears
```

## 💡 Feature Requests

Have an idea? Open an issue with:

- **Use case** - Why is this needed?
- **Proposed solution** - How should it work?
- **Alternatives** - Other ways to solve it?
- **Examples** - Code showing how it would be used

## 📞 Getting Help

### Before Asking:

1. **Read the documentation**:
   - [README.md](README.md) - Overview
   - [TUTORIAL.md](TUTORIAL.md) - Step-by-step guide
   - [OFFSETS.md](OFFSETS.md) - Offset reference

2. **Search existing issues** - Someone may have asked already

3. **Check discussions** - Ongoing conversations about features

### Asking Questions:

Use **GitHub Discussions** (not issues) for:

- "How do I...?"
- "Why does...?"
- "What's the best way to...?"

Include:

- What you're trying to accomplish
- What you've tried so far
- Relevant code snippets
- Error messages (if any)

### Example Question:

❌ **Bad:**

```
How do I read heading?
```

✅ **Good:**

````
## Reading heading returns incorrect values

I'm trying to read the heading offset but getting values that don't match X-Plane.

**Code:**
```cpp
uint32_t raw = 0;
QueueRead(0x0238, 4, &raw);
Process();
std::cout << raw << "\n";  // Prints "49152"
````

X-Plane shows 270° but I'm not sure how to convert 49152 to degrees.

I read the OFFSETS.md but the formula `raw * 360.0 / 65536.0` gives me 270°, which is correct! But how do I know this formula works for all headings?

```

## 🏆 Recognition

Contributors will be:
- Listed in project README
- Credited in commit history
- Mentioned in release notes

Significant contributions may be highlighted in documentation.

## 📜 Code of Conduct

### Be Respectful

- **Welcoming** - Treat everyone with respect
- **Patient** - Remember everyone was a beginner once
- **Constructive** - Focus on helping, not criticizing
- **Professional** - Keep discussions technical and on-topic

### Not Tolerated

- Harassment or discrimination
- Personal attacks
- Trolling or inflammatory comments
- Spam or off-topic content

**Violations will result in removal of comments/PRs and potential ban.**

## 🚀 Priority Areas

Current priorities (great places to start!):

### High Priority
- [ ] Additional offset implementations (see OFFSETS.md for list)
- [ ] Performance benchmarking suite
- [ ] Automated testing framework
- [ ] More real-world examples

### Medium Priority
- [ ] X-Plane dataref mapping table
- [ ] Compatibility testing with popular FSUIPC apps
- [ ] Video tutorials
- [ ] Diagram improvements

### Low Priority
- [ ] Linux/Wine support investigation
- [ ] Alternative IPC mechanisms (comparison)
- [ ] Historical FSUIPC version support

## 📝 License

By contributing, you agree that your contributions will be licensed under the MIT License.

---

## Questions?

- **General questions**: GitHub Discussions
- **Bug reports**: GitHub Issues
- **Security issues**: Email (see SECURITY.md)

**Thank you for helping make this the best IPC tutorial resource!** 🎉
```
