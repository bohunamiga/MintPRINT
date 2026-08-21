# Duplex printing

MintPRINT keeps duplex **off by default**. After **Query Printer**, the
**Sides** selector is enabled only when the printer reports all of the IPP
features needed to keep several Amiga pages in one physical print job:

- `sides-supported` includes `two-sided-long-edge` and/or
  `two-sided-short-edge`;
- `operations-supported` includes Create-Job and Send-Document;
- `multiple-document-jobs-supported` is true; and
- `multiple-document-handling-supported` includes `single-document`.

If any of those signals is missing, MintPRINT leaves the selector at
**One-sided** and ghosted. It does not guess: a printer accepting the `sides`
attribute is not enough when every Amiga page would otherwise be submitted as
a separate IPP job.

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

1. sends Create-Job once, including `sides` and
   `multiple-document-handling=single-document`;
2. sends every completed Amiga page with Send-Document and
   `last-document=false`; and
3. closes the job from `DriverClose()` with an empty Send-Document carrying
   `last-document=true`.

The document engine is independent of duplex. JPEG, PWG Raster and PDF pages
all use the same multi-document IPP job flow when selected and supported.

## Testing a printer

1. Install the PR build and reboot so the new driver segment is loaded.
2. Open MintPrint Settings, select the printer and press **Query**.
3. Confirm that **Sides** offers only the duplex modes reported by the printer.
4. Save **Duplex - long edge** and print a four-page portrait document.
   Expected: two sheets, with pages 1/2 and 3/4 paired.
5. Repeat with **One-sided**. Expected: four one-sided sheets and no change to
   page size, orientation or application behaviour.
6. If supported, test **Duplex - short edge** and confirm the reverse-side
   orientation is correct.

Enable **Debug** before a failed test and attach `T:MintPRINT-driver.log` to the
bug report. The log records Create-Job's job ID, every Send-Document result and
the final job-close result.
