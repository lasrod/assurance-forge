$ErrorActionPreference = 'Stop'
$path = 'src/app/actions/proposal_actions.cpp'
$abs = Resolve-Path $path
$lines = [System.IO.File]::ReadAllLines($abs)
$count = $lines.Length
Write-Output "COUNT_BEFORE=$count"

$anchors = @{
    1 = '#include "app/actions/proposal_actions.h"'
    30 = 'namespace app::actions {'
    31 = 'namespace {'
    389 = '} // namespace'
    391 = 'ProposalActions::ProposalActions(AppRuntimeState& state) : state_(state) {}'
}

$allOk = $true
foreach ($k in $anchors.Keys) {
    $expected = $anchors[$k].Trim()
    $actual = $lines[$k - 1].Trim()
    $ok = ($actual -eq $expected)
    Write-Output "ANCHOR_${k}_OK=$ok | actual: $actual"
    if (-not $ok) { $allOk = $false }
}

if (-not $allOk) { Write-Output "ABORT"; exit 1 }

# Build new file:
# Keep lines 1-29 (includes)
# Add: #include "app/actions/proposal_actions_internal.h"
# Keep line 30 (namespace app::actions {)
# Insert: using detail::* and using core::NowUtcString/TrimWhitespace + using core::reviews::BuildDraftReviewProposal
# Skip lines 31-388 (anon ns)
# Keep 389+ (blank + class methods)

$out = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt 29; $i++) { $out.Add($lines[$i]) }  # lines 1..29

# Insert the internal include just after the existing includes block (before namespace open)
$out.Add('#include "app/actions/proposal_actions_internal.h"')
$out.Add('')
$out.Add('namespace app::actions {')
$out.Add('')
$out.Add('using core::NowUtcString;')
$out.Add('using core::TrimWhitespace;')
$out.Add('using core::reviews::BuildDraftReviewProposal;')
$out.Add('using detail::ApplyProposalPreviewVisualState;')
$out.Add('using detail::CreateOperationFor;')
$out.Add('using detail::CreatedElementRef;')
$out.Add('using detail::DeleteProposalPatchFile;')
$out.Add('using detail::ElementTextTarget;')
$out.Add('using detail::GenerateCreateRef;')
$out.Add('using detail::IsContextLike;')
$out.Add('using detail::PreviewIdForProposalRef;')
$out.Add('using detail::ProposalRefForPreviewId;')
$out.Add('using detail::RemoveModeField;')
$out.Add('using detail::SameElementRef;')
$out.Add('using detail::SaveProject;')
$out.Add('using detail::SetStatus;')
$out.Add('using detail::TextTargetFor;')
$out.Add('using detail::TrackAffectedRef;')

# Skip lines 30-390 (namespace open + anon ns + closing brace + blank line)
# Keep from line 391 onward
for ($i = 390; $i -lt $count; $i++) {
    $out.Add($lines[$i])
}

$enc = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($abs, $out.ToArray(), $enc)
$after = [System.IO.File]::ReadAllLines($abs)
Write-Output "COUNT_AFTER=$($after.Length)"
Write-Output "LAST_LINE=$($after[-1])"
