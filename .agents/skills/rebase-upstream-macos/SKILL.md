---
name: rebase-upstream-macos
description: Re-apply each upstream omasnap commit after the recorded sync point, adapt compatible changes for macOS, run the complete checks, and open a pull request. Use when periodically syncing this fork with https://github.com/tobi/omasnap.
disable-model-invocation: true
---

# Re-apply upstream omasnap commits to the macOS fork

Run this workflow from the FOMOsnap repository root when the user explicitly
invokes this skill. The deliverable is a tested pull request against
`redox/fomosnap:main`; do not claim success until the PR exists.

This workflow intentionally does not rebase. It keeps the fork's existing
history in place and cherry-picks every upstream commit after the last
recorded sync point, in chronological order. Each upstream change therefore
gets its own reviewable commit and its own conflict-resolution point.

## Non-negotiable constraints

- Read `AGENTS.md` before changing code and follow its macOS-only boundaries.
- Preserve the macOS platform implementation in `src/mac/*.mm` and keep Apple
  headers out of non-Objective-C++ files.
- Port compatible upstream changes to shared/editor code; do not reintroduce
  Wayland, Hyprland, X11, or Linux-only build paths.
- Never use `git reset --hard`, `git clean`, a force push, or an automatic stash.
- Do not open a PR when the build or tests fail.
- Keep unrelated local work out of the sync branch.
- Treat `.github/upstream-sync` as authoritative. It must contain exactly one
  full upstream commit SHA followed by a newline.

## 1. Preflight and safety gate

1. Confirm the current repository and read `AGENTS.md`.
2. Run:

   ```bash
   git status --short --branch
   git remote -v
   ```

   Require a clean working tree. If any tracked, staged, or untracked file is
   present, stop and report the exact paths. Do not stash, clean, reset, or
   otherwise alter those files.
3. Require an `upstream` remote whose fetch URL is
   `https://github.com/tobi/omasnap` (an equivalent `.git` suffix is fine).
   Require an `origin` remote pointing at the FOMOsnap fork. If either remote
   is missing or points elsewhere, stop and ask the user to fix it.
4. Require `.github/upstream-sync` and read its single SHA:

   ```bash
   test "$(wc -l < .github/upstream-sync | tr -d ' ')" -eq 1
   last_upstream="$(tr -d '\n' < .github/upstream-sync)"
   test "$(printf '%s' "$last_upstream" | wc -c | tr -d ' ')" -eq 40
   case "$last_upstream" in
     *[!0-9a-f]*) exit 1 ;;
   esac
   git cat-file -e "$last_upstream^{commit}"
   ```

   Stop if the file is missing, contains anything other than one full SHA, or
   names an object that is not a commit.
5. Fetch both branch tips without merging:

   ```bash
   git fetch --prune upstream main
   git fetch --prune origin main
   ```

6. Require both the recorded commit and `origin/main` to be ancestors of
   `HEAD`:

   ```bash
   git merge-base --is-ancestor "$last_upstream" HEAD
   git merge-base --is-ancestor origin/main HEAD
   ```

   If either check fails, stop rather than silently omitting fork commits or
   applying changes from the wrong baseline.
7. Record the fetched upstream tip:

   ```bash
   upstream_sha="$(git rev-parse upstream/main)"
   ```

   Verify that `last_upstream` is an ancestor of `upstream/main`. If it is not,
   the marker points at the wrong upstream line and the workflow must stop.

## 2. Create an isolated sync branch

Record the starting branch and fork commit, then create a new branch from the
clean current `HEAD`; never alter the user's existing branch in place:

```bash
starting_branch="$(git branch --show-current)"
starting_fork_commit="$(git rev-parse HEAD)"
git switch -c "sync/upstream-$(git rev-parse --short "$upstream_sha")"
```

If that branch name already exists, append a unique suffix and create it
without replacing or checking out the existing branch.

Compute the exact upstream commits to re-apply:

```bash
git rev-list --reverse "$last_upstream..$upstream_sha"
```

If the list is empty, report that the fork is already synchronized at
`$last_upstream`, return to `$starting_branch`, and stop without creating a
PR.

Before applying anything, inspect both the upstream range and the fork work
that will remain in place:

```bash
git log --reverse --oneline --decorate "$last_upstream..$upstream_sha"
git log --oneline --decorate "$last_upstream..$starting_fork_commit"
```

## 3. Re-apply and adapt each upstream commit

For each SHA printed by `git rev-list --reverse "$last_upstream..$upstream_sha"`,
run the following commands in order. Stop immediately if `git cherry-pick`
fails; do not let a shell loop continue while the repository is in a conflict
state. Do not squash the commits or replace this process with a rebase or
merge:

```bash
for upstream_commit in $(git rev-list --reverse "$last_upstream..$upstream_sha"); do
    git show --stat --oneline "$upstream_commit"
    git cherry-pick "$upstream_commit" || exit 1
done
```

After resolving a conflict and running `git cherry-pick --continue`, resume
with the next upstream SHA from the printed list; do not restart the complete
loop and attempt to cherry-pick commits that already succeeded.

For every conflict:

1. Run `git status` and identify the upstream commit and files involved.
2. Read the complete conflicting files and the upstream commit diff.
3. Preserve macOS behavior and apply the compatible upstream behavior manually.
   Do not resolve a conflict by taking all of one side.
4. Check the project boundaries after each resolution:
   - platform code remains under `src/mac/`;
   - shared code remains plain C++23/Qt;
   - macOS capture, window management, OCR, hotkeys, notifications, login-item
     behavior, and test fakes remain intact;
   - the agent never gates startup on Screen Recording permission.
5. Stage only the resolved files and continue the current cherry-pick:

   ```bash
   git add <resolved-files>
   GIT_EDITOR=true git cherry-pick --continue
   ```

If a conflict cannot be resolved confidently, run
`git cherry-pick --abort` on the new sync branch, leave the original branch
untouched, and report the upstream commit, files, and decision needed from the
user. Do not use `git cherry-pick --skip` to hide an unresolved compatibility
decision.

After all upstream commits apply, record the new sync point. This is a
fork-owned bookkeeping commit and must be included in the pull request:

```bash
printf '%s\n' "$upstream_sha" > .github/upstream-sync
git add .github/upstream-sync
git commit -m "Record upstream sync at $(git rev-parse --short "$upstream_sha")"
```

Inspect the complete result, including the bookkeeping commit:

```bash
git diff --check
git diff --stat "$starting_fork_commit"..HEAD
git log --oneline --decorate "$starting_fork_commit"..HEAD
```

Use `rg` to check changed build and source files for accidental
Wayland/Hyprland/X11 dependencies. Adapt the code and tests when upstream
behavior is useful but assumes Linux. Keep the changes narrowly focused on
making upstream functionality work on macOS.

## 4. Verify before publishing

Run the repository's complete verification command:

```bash
make check
```

This command must finish successfully, including the headless smoke suite and
available static analysis. If it fails:

1. Determine whether the failure is from an upstream commit, a macOS
   adaptation, or the environment.
2. Fix code or tests only when the fix is clearly required for this sync.
3. Rerun `make check` as needed.

Do not weaken, skip, or delete a test to make the sync pass. If the failure is
environmental or requires a product decision, stop without publishing and
report the command, failure, and current branch.

Before pushing, require:

```bash
git status --short
git diff --check
```

The status must be clean. Any adaptation commit must be focused and must not
contain unrelated local work.

## 5. Push and open the pull request

Check GitHub authentication only after verification:

```bash
gh auth status
```

Check for an existing open PR from the sync branch. If none exists, publish
the new branch without force:

```bash
existing_pr="$(gh pr list --repo redox/fomosnap \
  --head "$(git branch --show-current)" --state open \
  --json url --jq '.[0].url')"
git push --set-upstream origin HEAD
```

If `$existing_pr` is empty, create one with:

```bash
gh pr create \
  --repo redox/fomosnap \
  --base main \
  --head "$(git branch --show-current)" \
  --title "Apply upstream omasnap commits to the macOS port" \
  --body "$(cat <<'EOF'
## Summary

- Re-apply upstream `tobi/omasnap` `main` commits after the recorded sync point.
- Adapt upstream shared functionality while preserving the native macOS platform layer.
- Record the resulting upstream tip in `.github/upstream-sync`.

## Test plan

- [x] `make check`
- [x] `git diff --check`

## Review notes

List any upstream commits that required non-trivial macOS adaptation and call
out behavior that was intentionally not ported.
EOF
)"
```

If an open PR already exists, update its body with the current commit range
and test result instead of creating a duplicate:

```bash
gh pr edit "$existing_pr" --body "$(cat <<'EOF'
## Summary

- Re-apply upstream `tobi/omasnap` `main` commits after the recorded sync point.
- Adapt upstream shared functionality while preserving the native macOS platform layer.
- Record the resulting upstream tip in `.github/upstream-sync`.

## Test plan

- [x] `make check`
- [x] `git diff --check`
EOF
)"
```

Return the PR URL, the previous and new upstream SHAs, the re-applied commit
list, and the exact verification result.
