### Verify Binaries

#### Preparation:

Make sure you obtain the proper release signing keys and verify the fingerprint with several independent sources.

```sh
$ gpg --fingerprint "Bitcoin SV binary release signing key"
pub   4096R/6431FF95 2019-02-08 [expires: 2021-02-07]
      Key fingerprint = 2E6B 6460 9F97 BD0C 3ABD  89E1 169E 3EC0 6431 FF95
uid                  Daniel Connolly (Bitcoin SV binary release signing key) <d.connolly@nchain.com>

$ gpg --fingerprint "Richard Mills"
pub   4096R/50757AA0 2019-07-23
      Key fingerprint = 7500 0FF3 A8FF B0A1 0627  C859 E2D3 79E0 5075 7AA0
uid                  Richard Mills <r.mills@nchain.com>
sub   4096R/EBCD82F3 2019-07-23
```

#### Usage:

This script attempts to download the signature file `SHA256SUMS.asc` from https://bitcoinsv.io.

It first checks if the signature passes, and then downloads the files specified in the file, and checks if the hashes of these files match those that are specified in the signature file.

The script returns 0 if everything passes the checks. It returns 1 if either the signature check or the hash check doesn't pass. If an error occurs the return value is 2.


```sh
./verify.sh 0.2.1
./verify.sh 1.0.0.beta2
./verify.sh 1.0.3
```

If you only want to download the binaries of certain platform, add the corresponding suffix, e.g.:

```sh
./verify.sh 0.1.1-arm
./verify.sh 1.0.3-linux
```

If you do not want to keep the downloaded binaries, specify anything as the second parameter.

```sh
./verify.sh 1.0.3 delete
```
