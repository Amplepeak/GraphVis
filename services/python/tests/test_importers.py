"""Round-trip every hand-written reader through import_to_arrow.

These are the formats GraphVis parses itself rather than delegating to a
third-party package - Sea-Bird CNV, ODV, NMEA, GPX/KML, bathymetric and
molecular XYZ, the four dive-computer log formats, the genomics tables, PDB /
CIF / SDF structures, QIF, fixed-width and pipe-separated text. Each case is a
small real-shaped fixture, so a regression in the parsing shows up here rather
than in the application.

Run with:  python tests/test_importers.py
"""
import os, tempfile, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from graphvis_science.data import importer as I

d = tempfile.mkdtemp()
out = os.path.join(d, 'out')

def w(name, text, mode='w'):
    p = os.path.join(d, name)
    with open(p, mode) as f: f.write(text)
    return p

cases = {}

cases['cnv'] = w('cast.cnv', """* Sea-Bird SBE 19plus Data File:
# name 0 = prDM: Pressure, Digiquartz [db]
# name 1 = t090C: Temperature [ITS-90, deg C]
# name 2 = sal00: Salinity, Practical [PSU]
*END*
   1.000   18.4210   35.1200
   2.000   18.3990   35.1210
   3.000   18.1120   35.1450
   4.000   17.8880   35.1660
""")

cases['odv'] = w('cruise.odv', "//<Creator>ODV</Creator>\n//\nStation\tDepth\tTemp\tSal\nA1\t10\t18.2\t35.1\nA1\t20\t17.4\t35.2\nA2\t10\t18.9\t35.0\n")

cases['nmea'] = w('track.nmea', """$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
$SDDBT,32.8,f,10.0,M,5.5,F*06
$GPGGA,123520,4807.048,N,01131.010,E,1,08,0.9,545.1,M,46.9,M,,*47
$SDDBT,34.4,f,10.5,M,5.7,F*06
$SDMTW,18.4,C*1A
""")

cases['gpx'] = w('dive.gpx', """<?xml version="1.0"?>
<gpx version="1.1" xmlns="http://www.topografix.com/GPX/1/1"><trk><trkseg>
<trkpt lat="50.1" lon="-5.2"><ele>0</ele><time>2024-01-01T10:00:00Z</time></trkpt>
<trkpt lat="50.11" lon="-5.21"><ele>1</ele><time>2024-01-01T10:01:00Z</time></trkpt>
<trkpt lat="50.12" lon="-5.22"><ele>2</ele><time>2024-01-01T10:02:00Z</time></trkpt>
</trkseg></trk></gpx>""")

cases['kml'] = w('sites.kml', """<?xml version="1.0"?>
<kml xmlns="http://www.opengis.net/kml/2.2"><Document>
<Placemark><name>Wreck</name><Point><coordinates>-5.2,50.1,-32</coordinates></Point></Placemark>
<Placemark><name>Reef</name><LineString><coordinates>-5.3,50.2,-12 -5.31,50.21,-14</coordinates></LineString></Placemark>
</Document></kml>""")

cases['xyz-bathy'] = w('bathy.xyz', "-5.20 50.10 -32.4\n-5.21 50.11 -33.1\n-5.22 50.12 -35.0\n-5.23 50.13 -36.2\n")
cases['xyz-mol']   = w('mol.xyz', "3\nwater\nO 0.000 0.000 0.000\nH 0.758 0.586 0.000\nH -0.758 0.586 0.000\n")

cases['uddf'] = w('log.uddf', """<?xml version="1.0"?>
<uddf xmlns="http://www.streit.cc/uddf/3.2/"><profiledata><repetitiongroup><dive>
<samples>
<waypoint><divetime>0</divetime><depth>0.0</depth><temperature>291.0</temperature></waypoint>
<waypoint><divetime>60</divetime><depth>8.4</depth><temperature>290.2</temperature></waypoint>
<waypoint><divetime>120</divetime><depth>18.1</depth><temperature>288.9</temperature></waypoint>
</samples></dive></repetitiongroup></profiledata></uddf>""")

cases['ssrf'] = w('log.ssrf', """<divelog><dives><dive number='1'>
<divecomputer><sample time='0:00 min' depth='0.0 m' temp='18.0 C'/>
<sample time='0:30 min' depth='6.2 m' temp='17.4 C'/>
<sample time='1:00 min' depth='14.8 m' temp='16.1 C'/>
</divecomputer></dive></dives></divelog>""")

cases['dl7'] = w('log.zxu', "FSH|1|2|A|X\nZDP{\n0|0.0|210|18.4|0\n0.5|6.2|205|17.9|12\n1.0|14.8|198|16.2|17\nZDP}\n")

cases['sml'] = w('move.sml', """<sml><DeviceLog><Samples>
<Sample><Time>0</Time><Depth>0.0</Depth><Temperature>291.1</Temperature></Sample>
<Sample><Time>10</Time><Depth>4.2</Depth><Temperature>290.8</Temperature></Sample>
<Sample><Time>20</Time><Depth>9.9</Depth><Temperature>289.4</Temperature></Sample>
</Samples></DeviceLog></sml>""")

cases['vcf'] = w('v.vcf', "##fileformat=VCFv4.2\n#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n1\t100\t.\tA\tG\t50\tPASS\tDP=10\n1\t200\t.\tC\tT\t99\tPASS\tDP=22\n")
cases['bed'] = w('r.bed', "chr1\t100\t200\tpeak1\t55\t+\nchr1\t300\t450\tpeak2\t80\t-\nchr2\t10\t60\tpeak3\t30\t+\n")
cases['gff'] = w('a.gff3', "##gff-version 3\nchr1\tsrc\tgene\t100\t500\t.\t+\t.\tID=g1\nchr1\tsrc\texon\t120\t300\t.\t+\t.\tID=e1\n")
cases['fasta'] = w('s.fasta', ">seq1 first\nACGTACGTGG\nCCGG\n>seq2 second\nTTTTAAAACCCCGGGG\n")
cases['fastq'] = w('s.fastq', "@r1\nACGTACGTAC\n+\nIIIIIIIIII\n@r2\nGGCCTTAAGG\n+\nHHHHHHHHHH\n")
cases['pdb'] = w('p.pdb', """ATOM      1  N   MET A   1      38.428  13.104   4.191  1.00 32.05           N
ATOM      2  CA  MET A   1      37.222  12.286   4.383  1.00 30.11           C
ATOM      3  C   MET A   1      36.030  13.121   4.833  1.00 28.44           C
""")
cases['cif'] = w('p.cif', """data_x
loop_
_atom_site.group_PDB
_atom_site.id
_atom_site.type_symbol
_atom_site.Cartn_x
_atom_site.Cartn_y
_atom_site.Cartn_z
ATOM 1 N 38.428 13.104 4.191
ATOM 2 C 37.222 12.286 4.383
ATOM 3 C 36.030 13.121 4.833
#
""")
cases['sdf'] = w('m.sdf', """water
  test

  3  2  0  0  0  0            999 V2000
    0.0000    0.0000    0.0000 O   0  0
    0.7580    0.5860    0.0000 H   0  0
   -0.7580    0.5860    0.0000 H   0  0
M  END
$$$$
""")
cases['fwf'] = w('t.fwf', "name    value\nalpha    1.50\nbeta     2.25\ngamma    3.75\n")
cases['psv'] = w('t.psv', "a|b|c\n1|2|3\n4|5|6\n7|8|9\n")
cases['qif'] = w('t.qif', "!Type:Bank\nD01/02/2024\nT-25.40\nPCoffee\n^\nD02/02/2024\nT-100.00\nPFuel\n^\nD03/02/2024\nT2500.00\nPSalary\n^\n")

ok = fail = 0
for label, path in sorted(cases.items()):
    try:
        r = I.import_to_arrow(path, out)
        print(f"  ok   {label:12s} {r['kind']:22s} rows={r['rows']:4d} cols={len(r['columns'])} -> {r['numeric_columns'][:4]}")
        ok += 1
    except Exception as exc:
        print(f"  FAIL {label:12s} {type(exc).__name__}: {exc}")
        fail += 1

# ---------------------------------------------------------------------------
# Regressions.
#
# Each of these shipped, and none of them was caught by the round-trip above,
# because a fixture written to exercise a reader is written the way the reader
# expects. These are written the way real files actually arrive: with header
# lines, with quoting, compressed.
import gzip

# A SAM header is three tab-separated fields; read_csv infers its column count
# from the first line it reads, so with on_bad_lines='skip' every real
# eleven-field alignment row was thrown away as malformed. BED's optional
# 'track' and 'browser' lines did the same.
regressions = {}
regressions['sam @ header'] = (w('reads.sam',
    "@HD\tVN:1.6\tSO:coordinate\n"
    "@SQ\tSN:chr1\tLN:248956422\n"
    "@PG\tID:bwa\tPN:bwa\tVN:0.7.17\n"
    "r1\t99\tchr1\t1000\t60\t10M\t=\t1200\t210\tACGTACGTAC\tIIIIIIIIII\n"
    "r2\t147\tchr1\t1200\t55\t10M\t=\t1000\t-210\tTGCATGCATG\tIIIIIIIIII\n"
    "r3\t83\tchr1\t1500\t42\t10M\t=\t1300\t-210\tGGGGCCCCAA\tIIIIIIIIII\n"), 3)

regressions['bed track line'] = (w('peaks.bed',
    "browser position chr1:1-1000\n"
    "track name=peaks\n"
    "chr1\t100\t200\tp1\t55\t+\n"
    "chr1\t300\t450\tp2\t72\t-\n"), 2)

# _suffix() strips the compression suffix so the right reader is chosen, but
# only the pandas-backed readers decompress for themselves - the thirty or so
# hand-written ones got a raw deflate stream and blamed the file format.
regressions['vcf.gz'] = (w('calls.vcf.gz', gzip.compress(
    b"##fileformat=VCFv4.2\n"
    b"#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
    b"chr1\t100\t.\tA\tG\t50.0\tPASS\tDP=30\n"
    b"chr1\t200\t.\tC\tT\t99.5\tPASS\tDP=44\n"), 'wb'), 2)

regressions['cnv.gz'] = (w('cast3.cnv.gz', gzip.compress(
    b"* Sea-Bird SBE 9 Data File:\n"
    b"# name 0 = prDM: Pressure, Digiquartz [db]\n"
    b"# name 1 = t090C: Temperature [ITS-90, deg C]\n"
    b"# name 2 = sal00: Salinity, Practical [PSU]\n"
    b"*END*\n"
    b"    1.000   14.2100   35.1200\n"
    b"    2.000   14.1900   35.1250\n"
    b"    3.000   14.1500   35.1310\n"), 'wb'), 3)

# A blank line inside a Sea-Bird header used to end header parsing, so every
# later header line was appended to the cast as though it were a scan. The
# header lines here carry exactly three tokens, like a scan row, so the
# modal-width filter cannot quietly rescue it.
regressions['cnv blank header line'] = (w('cast4.cnv',
    "* Sea-Bird SBE 9 Data File:\n"
    "# name 0 = prDM: Pressure, Digiquartz [db]\n"
    "# name 1 = t090C: Temperature [ITS-90, deg C]\n"
    "# name 2 = sal00: Salinity, Practical [PSU]\n"
    "\n"
    "* System UpLoad\n"
    "* NMEA Latitude\n"
    "*END*\n"
    "    1.000   14.2100   35.1200\n"
    "    2.000   14.1900   35.1250\n"
    "    3.000   14.1500   35.1310\n"
    "    4.000   14.1000   35.1400\n"), 4)

# mmCIF quotes any value containing a space. A bare split() then gives the
# wrong field count, and the parser used to abandon the entire loop at the
# first such row - returning the atoms before it as if that were the structure.
regressions['cif quoted value'] = (w('quoted.cif',
    "data_TEST\n"
    "loop_\n"
    "_atom_site.group_PDB\n"
    "_atom_site.id\n"
    "_atom_site.label_atom_id\n"
    "_atom_site.Cartn_x\n"
    "_atom_site.Cartn_y\n"
    "_atom_site.Cartn_z\n"
    "ATOM 1 N 11.104 6.134 -6.504\n"
    "ATOM 2 'CA A' 12.560 6.331 -6.504\n"
    "ATOM 3 C 13.100 7.011 -5.240\n"
    "ATOM 4 O 14.200 7.500 -5.100\n"), 4)

print()
for label, (path, expected_rows) in sorted(regressions.items()):
    try:
        r = I.import_to_arrow(path, out)
        if r['rows'] != expected_rows:
            print(f"  FAIL {label:24s} expected {expected_rows} rows, got {r['rows']}")
            fail += 1
        else:
            print(f"  ok   {label:24s} rows={r['rows']:4d} cols={len(r['columns'])}")
            ok += 1
    except Exception as exc:
        print(f"  FAIL {label:24s} {type(exc).__name__}: {exc}")
        fail += 1

print(f"\n{ok} passed, {fail} failed")
def test_suite() -> None:
    """Report this file's result to pytest instead of aborting collection.

    These suites predate pytest: the body above runs its checks at import and
    then called sys.exit, which made `pytest tests/` abort the whole directory
    with an INTERNALERROR - so the six oldest and largest suites could only ever
    be run one file at a time, and CI had to special-case them.

    Running the file directly still behaves exactly as before.
    """
    assert fail == 0, (
        f"{fail} checks failed - run `python tests/test_importers.py` for the detail")


if __name__ == "__main__":
    sys.exit(1 if fail else 0)
