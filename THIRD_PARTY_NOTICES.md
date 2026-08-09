# Third-party notices

This project fetches pinned source revisions of third-party cryptographic and
systems libraries during CMake configuration. Those components remain governed
by their respective licenses and notices:

- [libOTe](https://github.com/osu-crypto/libOTe)
- [cryptoTools](https://github.com/ladnir/cryptoTools)
- [coproto](https://github.com/Visa-Research/coproto)
- [macoro](https://github.com/Visa-Research/macoro)
- [secure-join](https://github.com/ladnir/secure-join)
- [libsodium](https://github.com/jedisct1/libsodium)
- [Boost](https://www.boost.org/)
- [libdivide](https://github.com/ridiculousfish/libdivide)
- [bitpolymul](https://github.com/ladnir/bitpolymul)

The exact direct dependency revisions are recorded in
`cmake/Dependencies.cmake`; transitive dependency revisions are selected by the
pinned libOTe build. Review the license files in the fetched sources before
redistributing binaries.
