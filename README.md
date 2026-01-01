# milk-streamtelemetry-resample

Resample telemetry streams to a common timeframe

## Compilation

Coded in C for speed, compilation with gcc and cmake.
Uses CFITSIO library to load/write FITS files.

```
mkdir build
cd build
cmake ..
make
make install
```

## Usage

```
milk-streamtelemetry-resample <teldir> <sname> <tstart> <tend> <dt>

# teldir    telemetry directory
# sname     stream name
# tstart    start UT time, unix time [s]
# tend      end UT time, unix time, or relative to start time if first char is +
# dt        output time sampling [s]
```

The tstart and tend times can be unix seconds (floating point number, unit second) or in the form `UTYYYYMMDDTHH:HH:SS.SSS`. In the second form, tailing number (seconds for example) are optional and will be set to zero if not specified. The tend argument may be of the form `+SS.SSSS` (relative seconds to tstart) or `+MM:SS.SSSS` or `+HH:MM:SS.SSS` for longer time ranges.

The program will first display the start and end time in both unix seconds and UT date formats, with the time duration (tend-tstart) also given.

The program will then scan subdirectory(ies) of the form `YYYYMMDD/sname` for timing files, per the convention given in the [milk telemetry file format](https://github.com/oguyon/milk-streamtelemetry-scan/blob/main/TelemetryFormat.md). For example, if given this input:
```
milk-streamtelemetry-resample /mnt/data fastcam UT20251204T12:10 +12:05 0.01
```
the program will look for files of the form `/mnt/data/20251204/fastcam/fastcam_hh:mm:ss.sssssssss.txt` whith the `hh:mm:ss.sssssssss` part of the filename falling within  `12:10:00.000` and `12:12:05`. Additionally, the previous file will be included in the list of files scanned, as the time sample in the filename only shows the time at the beginning of sequence captured by the file. The program will list all such files to be scanned.

## Output

The program generates an ASCII output file named `<sname>.resample.txt`. This file contains the list of input frames overlapping with the specified time range. The file starts with a header (lines starting with `#`) describing the columns.

The columns are:
1. **Global frame index**: Frame index starting at 0 for the first frame overlapping with the time range.
2. **Frame start time**: Frame start time in Unix seconds. This is assumed to be equal to the end time of the previous frame.
3. **Frame end time**: Frame end time in Unix seconds (acquisition time end).
4. **Source filename**: The `.txt` file where the frame was found.
5. **Local frame index**: The frame index within the source `.txt` file.
6. **Resampled start time**: Frame start time in the new resampled time unit (floating point number, 0.0 at `tstart`, incrementing by 1.0 for every `dt`).
7. **Resampled end time**: Frame end time in the new resampled time unit.

## Testing

The repo comes with a sample of telemetry files (.txt and .fits.header files). To test the program, run in the build directory:
```
./milk-streamtelemetry-resample ../telemetrysample/ ocam2d UT20251106T10:20 UT20251106T10:21 0.01
```
