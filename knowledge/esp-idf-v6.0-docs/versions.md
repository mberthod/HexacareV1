<!-- Source: _sources/versions.rst.txt (ESP-IDF v6.0 documentation) -->

# ESP-IDF Versions

The ESP-IDF GitHub repository is updated regularly, especially the master branch where new development takes place.

For production use, there are also stable releases available.

## Releases

The documentation for the current stable release version can always be found at this URL:

<https://docs.espressif.com/projects/esp-idf/en/stable/>

Documentation for the latest version (master branch) can always be found at this URL:

<https://docs.espressif.com/projects/esp-idf/en/latest/>

The full history of releases can be found on the GitHub repository [Releases page](https://github.com/espressif/esp-idf/releases). There you can find release notes, links to each version of the documentation, and instructions for obtaining each version.

<!-- Only for: html -->
Another place to find documentation for all current releases is the documentation page, where you can go to the upper-left corner and click the version dropdown (between the target dropdown and the search bar). You can also use this dropdown to switch between versions of the documentation.

![image](/../_static/choose_version.png)

Documentation for older versions are also still available:

## Which Version Should I Start With?

- For production purposes, use the [current stable version](https://docs.espressif.com/projects/esp-idf/en/stable/). Stable versions have been manually tested, and are updated with "bugfix releases" which fix bugs without changing other functionality (see [Versioning Scheme](#versioning-scheme) for more details). Every stable release version can be found on the [Releases page](https://github.com/espressif/esp-idf/releases). Also refer to [Compatibility Between ESP-IDF Releases and Revisions of Espressif SoCs](https://github.com/espressif/esp-idf/blob/master/COMPATIBILITY.md) to make sure the ESP-IDF version you selected is compatible with the chip revision you are going to produce with.
- For prototyping, experimentation or for developing new ESP-IDF features, use the [latest version (master branch in Git)](https://docs.espressif.com/projects/esp-idf/en/latest/). The latest version in the master branch has all the latest features and has passed automated testing, but has not been completely manually tested ("bleeding edge").
- If a required feature is not yet available in a stable release, but you do not want to use the master branch, it is possible to check out a pre-release version or a release branch. It is recommended to start from a stable version and then follow the instructions for `updating-pre-release` or `updating-release-branch`.
- If you plan to use another project which is based on ESP-IDF, please check the documentation of that project to determine the version(s) of ESP-IDF it is compatible with.

See `updating` if you already have a local copy of ESP-IDF and wish to update it.

## Versioning Scheme

ESP-IDF uses [Semantic Versioning](https://semver.org/). This means that:

- Major Releases, like `v3.0`, add new functionality and may change functionality. This includes removing deprecated functionality.

  If updating to a new major release (for example, from `v2.1` to `v3.0`), some of your project's code may need updating and functionality may need to be re-tested. The release notes on the [Releases page](https://github.com/espressif/esp-idf/releases) include lists of Breaking Changes to refer to.

- Minor Releases like `v3.1` add new functionality and fix bugs but will not change or remove documented functionality, or make incompatible changes to public APIs.

  If updating to a new minor release (for example, from `v3.0` to `v3.1`), your project's code does not require updating, but you should re-test your project. Pay particular attention to the items mentioned in the release notes on the [Releases page](https://github.com/espressif/esp-idf/releases).

- Bugfix Releases like `v3.0.1` only fix bugs and do not add new functionality.

  If updating to a new bugfix release (for example, from `v3.0` to `v3.0.1`), you do not need to change any code in your project, and you only need to re-test the functionality directly related to bugs listed in the release notes on the [Releases page](https://github.com/espressif/esp-idf/releases).

## Support Periods

Each ESP-IDF major and minor release version has an associated support period. After this period, the release is End of Life and no longer supported.

The [ESP-IDF Support Period Policy](https://github.com/espressif/esp-idf/blob/master/SUPPORT_POLICY.md) explains this in detail, and describes how the support periods for each release are determined.

Each release on the [Releases page](https://github.com/espressif/esp-idf/releases) includes information about the support period for that particular release.

As a general guideline:

- If starting a new project, use the latest stable release.
- If you have a GitHub account, click the "Watch" button in the top-right of the [Releases page](https://github.com/espressif/esp-idf/releases) and choose "Releases only". GitHub will notify you whenever a new release is available. Whenever a bug fix release is available for the version you are using, plan to update to it.
- If possible, periodically update the project to a new major or minor ESP-IDF version (for example, once a year.) The update process should be straightforward for Minor updates, but may require some planning and checking of the release notes for Major updates.
- Always plan to update to a newer release before the release you are using becomes End of Life.

Each ESP-IDF major and minor release (V4.1, V4.2, etc) is supported for 30 months after the initial stable release date.

Supported means that the ESP-IDF team will continue to apply bug fixes, security fixes, etc to the release branch on GitHub, and periodically make new bugfix releases as needed.

Support period is divided into "Service" and "Maintenance" period:

| Period      | Duration  | Recommended for new projects? |
|-------------|-----------|-------------------------------|
| Service     | 12 months | Yes                           |
| Maintenance | 18 months | No                            |

During the Service period, bugfixes releases are more frequent. In some cases, support for new features may be added during the Service period (this is reserved for features which are needed to meet particular regulatory requirements or standards for new products, and which carry a very low risk of introducing regressions.)

During the Maintenance period, the version is still supported but only bugfixes for high severity issues or security issues will be applied.

Using an "In Service" version is recommended when starting a new project.

Users are encouraged to upgrade all projects to a newer ESP-IDF release before the support period finishes and the release becomes End of Life (EOL). It is our policy to not continue fixing bugs in End of Life releases.

Pre-release versions (betas, previews, `-rc` and `-dev` versions, etc) are not covered by any support period. Sometimes a particular feature is marked as "Preview" in a release, which means it is also not covered by the support period.

The ESP-IDF Programming Guide has information about the [different versions of ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/versions.html) (major, minor, bugfix, etc).

![image](https://dl.espressif.com/dl/esp-idf/support-periods.svg)

## Checking the Current Version

The local ESP-IDF version can be checked by using idf.py:

    idf.py --version

The ESP-IDF version is also compiled into the firmware and can be accessed (as a string) via the macro `IDF_VER`. The default ESP-IDF bootloader prints the version on boot (The version information is not always updated if the code in the GitHub repo is updated, it only changes if there is a clean build or if that particular source file is recompiled).

If writing code that needs to support multiple ESP-IDF versions, the version can be checked at compile time using `compile-time macros <idf-version-h>`.

Examples of ESP-IDF versions:

<table>
<thead>
<tr class="header">
<th>Version String</th>
<th>Meaning</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><p><code>v3.2-dev-306-gbeb3611ca</code></p></td>
<td><p>Master branch pre-release.<br />
- <code>v3.2-dev</code> - in development for version 3.2.<br />
- <code>306</code> - number of commits after v3.2 development started.<br />
- <code>beb3611ca</code> - commit identifier.</p></td>
</tr>
<tr class="even">
<td><code>v3.0.2</code></td>
<td><p>Stable release, tagged <code>v3.0.2</code>.</p></td>
</tr>
<tr class="odd">
<td><p><code>v3.1-beta1-75-g346d6b0ea</code></p></td>
<td><p>Beta version in development (on a <code class="interpreted-text" role="ref">release branch &lt;updating-release-branch&gt;</code>).<br />
- <code>v3.1-beta1</code> - pre-release tag.<br />
- <code>75</code> - number of commits after the pre-release beta tag was assigned.<br />
- <code>346d6b0ea</code> - commit identifier.</p></td>
</tr>
<tr class="even">
<td><p><code>v3.0.1-dirty</code></p></td>
<td><p>Stable release, tagged <code>v3.0.1</code>.<br />
- <code>dirty</code> means that there are modifications in the local ESP-IDF directory.</p></td>
</tr>
</tbody>
</table>

## Git Workflow

The development (Git) workflow of the Espressif ESP-IDF team is as follows:

- New work is always added on the master branch (latest version) first. The ESP-IDF version on `master` is always tagged with `-dev` (for "in development"), for example `v3.1-dev`.
- Changes are first added to an internal Git repository for code review and testing but are pushed to GitHub after automated testing passes.
- When a new version (developed on `master`) becomes feature complete and "beta" quality, a new branch is made for the release, for example `release/v3.1`. A pre-release tag is also created, for example `v3.1-beta1`. You can see a full [list of branches](https://github.com/espressif/esp-idf/branches) and a [list of tags](https://github.com/espressif/esp-idf/tags) on GitHub. Beta pre-releases have release notes which may include a significant number of Known Issues.
- As testing of the beta version progresses, bug fixes will be added to both the `master` branch and the release branch. New features for the next release may start being added to `master` at the same time.
- Once testing is nearly complete a new release candidate is tagged on the release branch, for example `v3.1-rc1`. This is still a pre-release version.
- If no more significant bugs are found or reported, then the final Major or Minor Version is tagged, for example `v3.1`. This version appears on the [Releases page](https://github.com/espressif/esp-idf/releases).
- As bugs are reported in released versions, the fixes will continue to be committed to the same release branch.
- Regular bugfix releases are made from the same release branch. After manual testing is complete, a bugfix release is tagged (i.e., `v3.1.1`) and appears on the [Releases page](https://github.com/espressif/esp-idf/releases).

## Updating ESP-IDF

Updating ESP-IDF depends on which version(s) you wish to follow:

- `updating-stable-releases` is recommended for production use.
- `updating-master` is recommended for the latest features, development use, and testing.
- `updating-release-branch` is a compromise between the first two.

> **Note**
>
> >
> Note

These guides assume that you already have a local copy of ESP-IDF cloned. To get one, check Step 2 in the `Getting Started </get-started/index>` guide for any ESP-IDF version.

### Updating to Stable Release

To update to a new ESP-IDF release (recommended for production use), this is the process to follow:

- Check the [Releases page](https://github.com/espressif/esp-idf/releases) regularly for new releases.
- When a bugfix release for the version you are using is released (for example, if using `v3.0.1` and `v3.0.2` is released), check out the new bugfix version into the existing ESP-IDF directory.
- In Linux or macOS system, please run the following commands to update the local branch to vX.Y.Z:

``` bash
cd $IDF_PATH
git fetch
git checkout vX.Y.Z
git submodule update --init --recursive
```

- In the Windows system, please replace `cd $IDF_PATH` with `cd %IDF_PATH%`.
- When major or minor updates are released, check the Release Notes on the releases page and decide if you want to update or to stay with your current release. Updating is via the same Git commands shown above.

> **Note**
>
> ### Updating to a Pre-Release Version

It is also possible to `git checkout` a tag corresponding to a pre-release version or release candidate, the process is the same as `updating-stable-releases`.

Pre-release tags are not always found on the [Releases page](https://github.com/espressif/esp-idf/releases). Consult the [list of tags](https://github.com/espressif/esp-idf/tags) on GitHub for a full list. Caveats for using a pre-release are similar to `updating-release-branch`.

### Updating to Master Branch

> **Note**
>
> To use the latest version on the ESP-IDF master branch, this is the process to follow:

- In Linux or macOS system, please run the following commands to check out to the master branch locally:

``` bash
cd $IDF_PATH
git checkout master
git pull
git submodule update --init --recursive
```

- In the Windows system, please replace `cd $IDF_PATH` with `cd %IDF_PATH%`.
- Periodically, re-run `git pull` to pull the latest version of master. Note that you may need to change your project or report bugs after updating your master branch.
- To switch from master to a release branch or stable version, run `git checkout` as shown in the other sections.

> **Important**
>
> ### Updating to a Release Branch

In terms of stability, using a release branch is part-way between using the master branch and only using stable releases. A release branch is always beta quality or better, and receives bug fixes before they appear in each stable release.

You can find a [list of branches](https://github.com/espressif/esp-idf/branches) on GitHub.

For example, in Linux or macOS system, you can execute the following commands to follow the branch for ESP-IDF v3.1, including any bugfixes for future releases like `v3.1.1`, etc:

``` bash
cd $IDF_PATH
git fetch
git checkout release/v3.1
git pull
git submodule update --init --recursive
```

In the Windows system, please replace `cd $IDF_PATH` with `cd %IDF_PATH%`.

Each time you `git pull` this branch, ESP-IDF will be updated with fixes for this release.

> **Note**
>
> 