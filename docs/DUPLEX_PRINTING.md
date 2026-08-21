# Duplex printing

MintPRINT keeps duplex **off by default**. After **Query Printer**, the
**Sides** selector is enabled only when:

- `sides-supported` includes `two-sided-long-edge` and/or
  `two-sided-short-edge`;
- `document-format-supported` includes `image/pwg-raster`; and
- **Printer Engine** is set to **PWG Raster**.

If those conditions are not met, MintPRINT leaves the selector at
**One-sided** and ghosted. JPEG is a single-image format, and MintPRINT's
current PDF encoder produces one PDF per page, so neither can safely represent
one duplex job yet.

MintPRINT does not require IPP multi-document Jobs. The Brother MFC-J6930DW,
for example, supports duplex, Create-Job and Send-Document but explicitly
reports `multiple-document-jobs-supported=false`. Its supported route is one
multi-page PWG Raster document in one ordinary Print-Job.

## Sides choices

| MintPrint Settings | IPP value | Typical binding |
| --- | --- | --- |
| One-sided | `one-sided` | Print only on sheet fronts |
| Duplex - long edge | `two-sided-long-edge` | Book-style portrait pages |
| Duplex - short edge | `two-sided-short-edge` | Calendar/notepad-style portrait pages |

## Driver behaviour

The one-sided path is unchanged: every completed Amiga page uses the existing
IPP Print-Job submission. This preserves the already-tested Wordsworth,
ArtEffect and strip-printing behaviour.

With a duplex value selected, the driver instead:

1. opens one PWG Raster `RaS2` stream;
2. appends one 1796-byte PWG page header and raster body per completed Amiga
   page; and
3. submits the complete multi-page file once from `DriverClose()` using the
   existing IPP Print-Job operation with `sides=...`.

The file contains only one `RaS2` sync word. Every following header/body pair
is another page in that same document.

Query also stores `pwg-raster-document-sheet-back`. That capability tells the
driver which coordinate system the printer expects for reverse-side PWG
bitmaps. MintPRINT writes the matching PWG `Duplex`, `Tumble`,
`CrossFeedTransform` and `FeedTransform` header fields. When a reverse page
must be flipped (the Brother MFC-J6930DW reports `rotated`, for example), its
RGB rows are temporarily spooled to `T:` and replayed in the required order.
Only one scanline is held in memory; the temporary file is removed after the
page is encoded. If the printer omits this conditionally required capability,
MintPRINT uses the standard `normal` coordinate system.

## Testing a printer

1. Install the PR build and reboot so the new driver segment is loaded.
2. Open MintPrint Settings, select the printer and press **Query**.
3. Select **PWG Raster** and confirm that **Sides** offers only the duplex
   modes reported by the printer.
4. Save **Duplex - long edge** and print a four-page portrait document.
   Expected: two sheets, with pages 1/2 and 3/4 paired.
5. Repeat with **One-sided**. Expected: four one-sided sheets and no change to
   page size, orientation or application behaviour.
6. If supported, test **Duplex - short edge** and confirm the reverse-side
   orientation is correct.

Enable **Debug** before a failed test and attach `T:MintPRINT-driver.log` and
`T:MintPRINT-job.pwg` to the bug report. The log records the queried sheet-back
mode, each queued PWG page, backside transforms, and the final Print-Job result.
