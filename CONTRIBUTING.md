Contributing to Xenomai
=======================

Contributions to Xenomai are always welcome. This document explains the general
requirements on contributions and the recommended preparation steps. It also
sketches the typical integration process of patches.


Contribution Checklist
----------------------

- use git to manage your changes [*recommended*]

- follow Linux Kernel coding style [**required**]
    - see also [Linux kernel coding style](https://www.kernel.org/doc/html/latest/process/coding-style.html)
    - try out the checkpatch.pl script from the Linux kernel

- add the required copyright header to each new file introduced [**required**]

- structure patches logically, in small steps [**required**]
    - one separable functionality/fix/refactoring = one patch
    - do not mix those three into a single patch (e.g. first refactor, then
      add a new functionality that builds onto the refactoring)
    - after each patch, the tree still has to build and work, i.e. do not add
      even temporary breakages inside a patch series (helps when tracking down
      bugs)
    - use `git rebase -i` to restructure a patch series

- base patches on top of latest master or - if there are dependencies - on next
  (note: next is an integration branch that may change non-linearly) [**required**]

- test patches sufficiently AFTER the last edit (obvious, but...) [**required**]

- add signed-off to all patches [**required**]
    - to certify the "Developer's Certificate of Origin", see below
    - check with your employer when not working on your own!

- indicate if you think a patch fixes a bug present in a stable branch as well [*recommended*]
    - add a note to the cover letter of the patch series
    - or add some remark after the "---" separator of the patch itself

- post patches to mailing list [**required**]
    - use `git format-patch/send-email` if possible
    - send patches inline, do not append them
    - no HTML emails!
    - CC people who you think should look at the patches, e.g.
      - affected maintainers
      - someone who wrote a change that is fixed or reverted by you now
      - who commented on related changes in the recent past
      - who otherwise has expertise and is interested in the topic
    - pull requests on gitlab are only optional

- post follow-up version(s) if feedback requires this [**required**]

- send reminder if nothing happened after about two weeks [*recommended*]


Developer's Certificate of Origin 1.1
-------------------------------------

When signing-off a patch for this project like this

    Signed-off-by: Random J Developer <random@developer.example.org>

using your real name (no pseudonyms or anonymous contributions), you declare the
following:

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

See also [Sign your work - the Developer’s Certificate of Origin](https://www.kernel.org/doc/html/latest/process/submitting-patches.html#sign-your-work-the-developer-s-certificate-of-origin)
for further background on this process which was adopted from the Linux kernel.


Contribution Integration Process
--------------------------------

1. patch reviews performed on mailing list
    * at least by maintainers, but everyone is invited
    * feedback has to consider design, functionality and style
    * simpler and clearer code preferred, even if original code works fine

2. accepted patches merged into next branch

3. further testing done by community, including CI build tests, code analyzer
   runs, on-target tests

4. if no new problems or discussions showed up, acceptance into master
    * grace period for master: about 3 days
    * urgent fixes may be applied sooner

5. a stable-relevant patch is applied to the related stable branch after it was
   merged into master (except for patches that are stable-specific)

gitlab facilities are not used for the review process so that people can follow
all changes and related discussions at a single stop, the mailing list. This
may change in the future if gitlab should improve their email integration.
