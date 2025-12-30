#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fitsio.h"

#define MAX_PATH 1024
#define MAX_FILES 10000

// Struct to hold file info
typedef struct {
    char filepath[MAX_PATH];
    double tstart; // Unix timestamp
} FileEntry;

// Function prototypes
double parse_time_arg(const char *tstr, double relative_to);
double parse_ut_string(const char *ut_str);
double parse_filename_time(const char *filename);
void scan_files(const char *teldir, const char *sname, double tstart, double tend, FileEntry **files, int *count);
void print_scan_list(FileEntry *files, int count);
void print_time_info(double tstart, double tend);
int compare_files(const void *a, const void *b);
void format_time(double t, char *buffer, size_t size);

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <teldir> <sname> <tstart> <tend> <dt>\n", argv[0]);
        return 1;
    }

    const char *teldir = argv[1];
    const char *sname = argv[2];
    const char *tstart_str = argv[3];
    const char *tend_str = argv[4];
    // dt is available but not used for scanning logic yet, but required by spec
    // double dt = atof(argv[5]);

    double tstart = parse_time_arg(tstart_str, 0);
    if (tstart < 0) {
        fprintf(stderr, "Error parsing tstart: %s\n", tstart_str);
        return 1;
    }

    double tend = parse_time_arg(tend_str, tstart);
    if (tend < 0) {
        fprintf(stderr, "Error parsing tend: %s\n", tend_str);
        return 1;
    }

    // "The program will first display the start and end time in both unix seconds and UT date formats"
    print_time_info(tstart, tend);

    // Scan files
    FileEntry *files = NULL;
    int file_count = 0;
    scan_files(teldir, sname, tstart, tend, &files, &file_count);

    // "The program will list all such files to be scanned."
    print_scan_list(files, file_count);

    // Free memory
    if (files) free(files);

    // Note: The prompt asks to write code per instructions. The instructions in Usage example
    // end with "The program will list all such files to be scanned."
    // While the name suggests resampling, without specification on output format/destination,
    // and given the explicit behavior description, this completes the requirements described in Usage.

    return 0;
}

// Helper to check if string starts with prefix
int starts_with(const char *pre, const char *str) {
    size_t lenpre = strlen(pre);
    size_t lenstr = strlen(str);
    return lenstr < lenpre ? 0 : strncmp(pre, str, lenpre) == 0;
}

// Helper to parse UT string: UTYYYYMMDDTHH:MM:SS.SSS
double parse_ut_string(const char *ut_str) {
    if (!starts_with("UT", ut_str)) return -1.0;

    struct tm tm_val;
    memset(&tm_val, 0, sizeof(struct tm));

    // UTYYYYMMDDTHH:MM:SS.SSS
    // 0123456789012345678901
    // YYYY = 2-5
    // MM = 6-7
    // DD = 8-9
    // HH = 11-12
    // MM = 14-15
    // SS = 17-18
    // .SSS = 19...

    // Use sscanf to parse parts
    int year, month, day, hour, minute;
    double seconds = 0.0;

    // We handle the optional seconds part manually
    char tmp[64];
    strncpy(tmp, ut_str + 2, 63);
    tmp[63] = '\0';

    // Replace T with space for easier parsing
    char *t_ptr = strchr(tmp, 'T');
    if (t_ptr) *t_ptr = ' ';

    int scanned = sscanf(tmp, "%4d%2d%2d %2d:%2d:%lf",
                         &year, &month, &day, &hour, &minute, &seconds);

    if (scanned < 5) return -1.0;

    tm_val.tm_year = year - 1900;
    tm_val.tm_mon = month - 1;
    tm_val.tm_mday = day;
    tm_val.tm_hour = hour;
    tm_val.tm_min = minute;
    tm_val.tm_sec = (int)seconds;

    time_t raw_time = timegm(&tm_val);
    if (raw_time == -1) return -1.0;

    double fractional = seconds - (int)seconds;
    return (double)raw_time + fractional;
}

double parse_time_arg(const char *tstr, double relative_to) {
    if (starts_with("UT", tstr)) {
        return parse_ut_string(tstr);
    } else if (tstr[0] == '+') {
        // Relative time
        // Formats: +SS.SSSS, +MM:SS.SSSS, +HH:MM:SS.SSS
        double offset = 0.0;
        int h = 0, m = 0;
        double s = 0.0;

        // Count colons
        int colons = 0;
        const char *p = tstr;
        while (*p) {
            if (*p == ':') colons++;
            p++;
        }

        if (colons == 0) {
            // +SS.SSSS
            s = atof(tstr + 1);
        } else if (colons == 1) {
            // +MM:SS.SSSS
            sscanf(tstr + 1, "%d:%lf", &m, &s);
            offset = m * 60.0 + s;
            return relative_to + offset;
        } else if (colons == 2) {
            // +HH:MM:SS.SSS
            sscanf(tstr + 1, "%d:%d:%lf", &h, &m, &s);
            offset = h * 3600.0 + m * 60.0 + s;
            return relative_to + offset;
        }
        return relative_to + s;
    } else {
        // Assume unix seconds
        return atof(tstr);
    }
}

void format_time(double t, char *buffer, size_t size) {
    time_t raw_time = (time_t)t;
    double frac = t - raw_time;
    struct tm *tm_info = gmtime(&raw_time);

    char tmp[64];
    strftime(tmp, sizeof(tmp), "UT%Y%m%dT%H:%M:%S", tm_info);

    // append fractional part
    snprintf(buffer, size, "%s.%03d", tmp, (int)(frac * 1000 + 0.5));
}

void print_time_info(double tstart, double tend) {
    char start_buf[64], end_buf[64];
    format_time(tstart, start_buf, sizeof(start_buf));
    format_time(tend, end_buf, sizeof(end_buf));

    printf("Time scan:\n");
    printf("  Start: %.4f (%s)\n", tstart, start_buf);
    printf("  End:   %.4f (%s)\n", tend, end_buf);
    printf("  Duration: %.4f s\n", tend - tstart);
}

// Parses time from filename like sname_HH:MM:SS.SSSSSSSSS.txt
// We need the date from the directory context, so this function only returns seconds within the day
// Actually, it's better if we pass the full date context or construct it properly.
// But wait, the directory is YYYYMMDD.
// So we should construct the full unix time.
double parse_filename_time(const char *filename) {
    // Expected format: sname_HH:MM:SS.SSSSSSSSS.txt
    // Find the underscore
    const char *p = strrchr(filename, '_');
    if (!p) return -1.0;
    p++; // skip _

    int h, m;
    double s;
    if (sscanf(p, "%d:%d:%lf", &h, &m, &s) < 3) return -1.0;

    return h * 3600.0 + m * 60.0 + s;
}

int compare_files(const void *a, const void *b) {
    FileEntry *fa = (FileEntry *)a;
    FileEntry *fb = (FileEntry *)b;
    if (fa->tstart < fb->tstart) return -1;
    if (fa->tstart > fb->tstart) return 1;
    return 0;
}

// Get list of day directories YYYYMMDD between tstart and tend
// Actually, we can just iterate day by day.
void scan_files(const char *teldir, const char *sname, double tstart, double tend, FileEntry **files, int *count) {
    *files = malloc(MAX_FILES * sizeof(FileEntry));
    *count = 0;

    // We iterate from day of tstart to day of tend
    time_t t_iter_raw = (time_t)tstart;
    time_t t_end_raw = (time_t)tend;

    // Align t_iter to start of day
    struct tm *tm_iter = gmtime(&t_iter_raw);
    tm_iter->tm_hour = 0;
    tm_iter->tm_min = 0;
    tm_iter->tm_sec = 0;
    t_iter_raw = timegm(tm_iter);

    // We also need the previous day just in case the "previous file" is in previous day
    // But the logic "previous file will be included" suggests we should find the file just before tstart.
    // If tstart is 00:00:01, the previous file might be in previous day (23:59:59).
    // So let's start checking from (tstart - 24h) just to be safe?
    // Or simpler: checking day of tstart and day of tend is usually enough, but if tstart is close to midnight, we might need prev day.
    // Let's start from day of (tstart - padding). Say 1 day padding.

    time_t t_scan_start = t_iter_raw - 24 * 3600;

    while (t_scan_start <= t_end_raw) {
        struct tm *tm_scan = gmtime(&t_scan_start);
        char date_dir[20];
        sprintf(date_dir, "%04d%02d%02d", tm_scan->tm_year + 1900, tm_scan->tm_mon + 1, tm_scan->tm_mday);

        char dirpath[MAX_PATH];
        snprintf(dirpath, sizeof(dirpath), "%s/%s/%s", teldir, date_dir, sname);

        // Check if dir exists
        DIR *d = opendir(dirpath);
        if (d) {
            struct dirent *dir;
            while ((dir = readdir(d)) != NULL) {
                if (strstr(dir->d_name, ".txt") && strstr(dir->d_name, sname) == dir->d_name) {
                    // Parse time
                    double time_in_day = parse_filename_time(dir->d_name);
                    if (time_in_day >= 0) {
                        double file_abs_time = (double)t_scan_start + time_in_day;

                        // We store all files for now, then sort and filter
                        if (*count < MAX_FILES) {
                            snprintf((*files)[*count].filepath, MAX_PATH, "%s/%s", dirpath, dir->d_name);
                            (*files)[*count].tstart = file_abs_time;
                            (*count)++;
                        }
                    }
                }
            }
            closedir(d);
        }

        t_scan_start += 24 * 3600;
    }

    // Sort files by time
    qsort(*files, *count, sizeof(FileEntry), compare_files);

    // Now filter
    // We need all files overlapping [tstart, tend]
    // And "previous file will be included... as the time sample... shows the time at the beginning of sequence"
    // So we need the file where file.tstart <= tstart < file.next_tstart
    // OR just file.tstart <= tstart. The closest one.

    // Strategy: find the index of the first file with tstart > given_tstart.
    // The file immediately before that is the "previous file" (covering the start).
    // Then include all subsequent files until file.tstart >= tend.
    // Actually, if file starts before tend, it might contain data up to tend.

    // Let's identify the start index.
    int start_idx = -1;
    for (int i = 0; i < *count; i++) {
        if ((*files)[i].tstart > tstart) {
            start_idx = i - 1;
            break;
        }
    }
    // If all files are <= tstart, start_idx is count-1
    if (start_idx == -1 && *count > 0 && (*files)[*count-1].tstart <= tstart) {
        start_idx = *count - 1;
    }

    if (start_idx < 0) start_idx = 0; // If no file starts before tstart, start with first available?

    // Now collect relevant files into a new list or just mark them
    // We'll reuse the array by shifting or just creating a new one?
    // Let's create a temporary list
    FileEntry *filtered = malloc(MAX_FILES * sizeof(FileEntry));
    int f_count = 0;

    for (int i = start_idx; i < *count; i++) {
        // Condition to stop:
        // We need files that cover range up to tend.
        // A file starting at T contains data for some duration.
        // We don't know the duration without reading it, but generally we include files starting before tend.
        // If a file starts exactly at tend, it might be relevant?
        // Usage example: "look for files ... with filename falling within 12:10:00 and 12:12:05. Additionally, the previous file..."
        // So we include files with tstart in [tstart, tend], PLUS one previous.

        // My logic above found `start_idx` as the "previous file" (tstart <= query_tstart).
        // So we include it.
        // And we include any file where tstart <= tend.

        if ((*files)[i].tstart <= tend) {
             filtered[f_count++] = (*files)[i];
        } else {
            // Once we pass tend, do we stop?
            // Yes, a file starting after tend definitely doesn't contain data before tend (assuming chronological)
            break;
        }
    }

    free(*files);
    *files = filtered;
    *count = f_count;
}

void print_scan_list(FileEntry *files, int count) {
    for (int i = 0; i < count; i++) {
        printf("%s\n", files[i].filepath);
    }
}
