# Clean-Checkout Verification

## Result

```text
Verification date: 2026-08-12
Branch: sample
Verified commit: f6530794
Clean-checkout result: PASS
```

The sample was reproduced from an independent clone in a newly created `/tmp` directory. No build, output, or result file was copied from the development checkout. The committed `SAMPLE-test/input/hotdog.ply` was the only test input, so the documented no-argument command could be verified exactly as a new user would run it.

This was a **clean Git checkout on the same host operating system and toolchain**, not a fresh operating-system container. Docker, Podman, and nerdctl were unavailable. This limitation is stated explicitly so the result is not presented as stronger isolation than was actually tested.

## Environment

```text
Compiler: g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
CMake: 4.2.3
GNU Make: 4.4.1
```

## Reproduction procedure

An independent clone was created without using local hard links:

```bash
clean_root=$(mktemp -d /tmp/meshoptimizer-sample-clean.XXXXXX)

git clone --no-local --branch sample \
  /home/nahathai/summer/meshoptimizer \
  "$clean_root/repo"
```

The checkout initially reported no Git changes:

```bash
git -C "$clean_root/repo" status --short
```

The committed input checksum was checked:

```bash
sha256sum "$clean_root/repo/SAMPLE-test/input/hotdog.ply"
```

The original repository tests were run during the initial clean-checkout verification:

```bash
make -C "$clean_root/repo" check
```

After the input was added, the documented no-argument command was run from a second independent clone:

```bash
"$clean_root/repo/SAMPLE-test/run.sh"
```

Finally, Git status was checked again:

```bash
git -C "$clean_root/repo" status --short
```

It remained empty because all generated build, encoded, decoded, and result files are covered by the sample's `.gitignore` rules.

## Verified output

| Check | Result |
|---|---:|
| Repository `make check` | PASS |
| Sample configured from empty build directory | PASS |
| Sample compiled without warnings | PASS |
| Vertex decoder | PASS |
| Index decoder | PASS |
| Vertex count | 501,225 PASS |
| Face count | 1,002,315 PASS |
| Index count | 3,006,945 PASS |
| Vertex byte comparison | PASS |
| Index byte comparison | PASS |
| Geometry comparison | PASS |
| Overall strict lossless result | PASS |
| Committed input checksum | Expected SHA-256 PASS |
| Input checksum before/after | PASS |
| Git status after generated outputs | Clean |

Compression output from the clean checkout:

```text
combined_raw_bytes=20047380
combined_encoded_bytes=11213051
encoded_raw_percent=55.93
```

Input checksum before and after:

```text
770de889bd896117e1429b4b7e618d98ef2ca3f5d5b6d03da7492e7ab95a6b1f
```

## Conclusion

The sample builds and runs from a fresh checkout using only the documented toolchain and included HOTDOG PLY. It does not depend on the previous experiment folders, external input paths, old build products, hard-coded repository paths, or globally installed meshoptimizer files. Generated artifacts do not dirty the checkout.

A future container test can strengthen environment isolation when a container runtime is available, but no missing dependency or undocumented build step was found in this clean-checkout verification.
