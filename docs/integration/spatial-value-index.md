# Value-index integration evidence

The original C++ RBush and quickselect headers remain byte-identical to the faithful port. New behavior belongs to C ABI integration helpers and the separate Swift value facade.

## Contracts

- Mutable original RBush APIs remain unchanged. Value copies share read-only storage; mutation clones native nodes and mutable item geometry first. Stable external keys are not exposed as mutable items.
- Root box is a direct O(1) native root read, including explicit empty detection.
- Predicate search stops on first accepted item, not materialize-all then contains.
- Region summary separately bounds node-box reads and leaf-entry-box reads by maxNodeVisits. For96, at most96+96geometry reads; finaloutput≤maxRegions. Temporaryfrontier bounded by2×budget. Fullgeometryexport is not used.
- Clone duplicate aliases are retained inside the clone, while mutable geometry never aliases the original. Nativeclone avoids a second N-item strong identitycache andreserves finalmap beforebuilding.

## Validation

9 new helper/value tests pass, including duplicate/geometry clone independence, actual earlystop, point/emptyroot, concurrentread/forkwrite, budgetcounters andlargeleaf512items/maxEntries1000 nobulkentryscan. TSan scoped2tests pass; native strictwarnings+ASan/UBSan pass. Originalcore hash remains unchanged.

The existing compass summary tests preserve the 512-item separated-cloud gap assertion; all8 map/summary cases pass. An18-item case is additional coverage, not a replacement for the larger regression.

## Same existing fixture costs

No new randomgeometry was introduced. The16same canvas fixtures (5k/10k/50k/100k ×4distributions) supply write andsummary cost data; CSVs accompany this record. At100k, safecloneoptimization reducedrawfacade firstCOW from14–24ms to5.4–7.2ms; these are not the full metadata costs. Correctregion extraction costs6–358µs inthisrun, versusan earlierincorrect/coarser summary; differentquality is not treatedas alike-for-like speed comparison.

## Actual metadata layer: same original benchmark source

| Items | Old first shared write p50 | New optimized p50 | Old p95 | New p95 |
|---:|---:|---:|---:|---:|
|10000|309µs|645µs|374µs|676µs|
|50000|1674µs|3336µs|1968µs|3766µs|
|100000|3626µs|6932µs|3996µs|7162µs|

This is a measured ~1.9–2.1× firstwrite regression afteroptimization; do not conceal it behind fasterquery results. Steady100kupdate/delete/insert improved4.055/4.603/4.526µs→2.842/3.333/3.468µs. Rootread remainsconstant(~.08µs). These metadata timinginputs are the exact earlier metadata benchmark, distinct from the canvas queryfixtures. No device/App end-to-end gain is claimed.

Project integration keeps existing pure-value snapshot and revision contracts, avoids duplicate fullbbox/type/parentspatialindexes, andfeeds onlytheexistingNotebook compass internalcontentmap.
