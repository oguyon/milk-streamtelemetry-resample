#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include "fitsio.h"

// Struct to track active output frames in memory
typedef struct OutputFrame {
    long idx;
    float *data;
    struct OutputFrame *next;
} OutputFrame;

// Global linked list for active output frames
OutputFrame *active_frames = NULL;

void error_report(int status) {
    if (status) {
        fits_report_error(stderr, status);
        exit(status);
    }
}

// Find or create an output frame buffer
float* get_output_frame(long idx, long n_pixels) {
    OutputFrame *curr = active_frames;
    while (curr) {
        if (curr->idx == idx) {
            return curr->data;
        }
        curr = curr->next;
    }
    // Not found, create new
    OutputFrame *new_frame = (OutputFrame *)malloc(sizeof(OutputFrame));
    new_frame->idx = idx;
    new_frame->data = (float *)calloc(n_pixels, sizeof(float)); // Zero initialized
    new_frame->next = active_frames;
    active_frames = new_frame;
    return new_frame->data;
}

// Write and free output frames that are done (idx < threshold_idx)
void flush_frames(fitsfile *fptr, long threshold_idx, long naxis1, long naxis2) {
    OutputFrame *curr = active_frames;
    OutputFrame *prev = NULL;
    int status = 0;

    while (curr) {
        if (curr->idx < threshold_idx) {
            // Write this frame to FITS file
            // FITS 3D cube: NAXIS3 corresponds to time/frame index.
            // idx is 0-based index. FITS uses 1-based index for planes.
            // fits_write_subset expects fpixel/lpixel array coordinates.

            long fpixel[3] = {1, 1, curr->idx + 1};
            long lpixel[3] = {naxis1, naxis2, curr->idx + 1};

            // We can write the whole plane at once
            fits_write_subset(fptr, TFLOAT, fpixel, lpixel, curr->data, &status);
            error_report(status);

            // Free memory
            free(curr->data);
            OutputFrame *to_free = curr;

            if (prev) {
                prev->next = curr->next;
                curr = curr->next;
            } else {
                active_frames = curr->next;
                curr = active_frames;
            }
            free(to_free);
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}

// Flush all remaining frames
void flush_all_frames(fitsfile *fptr, long naxis1, long naxis2) {
    flush_frames(fptr, 2147483647, naxis1, naxis2); // Flush everything
}

// Construct the full path to the FITS file
// Checks for .fits first, then .fits.fz
void get_full_fits_path(char *full_path, const char *teldir, const char *filename, double timestamp) {
    char base_path[1024];

    if (teldir == NULL) {
        // Fallback: assume filename or current directory
        // Need to change extension
        strcpy(base_path, filename);
        char *ext = strrchr(base_path, '.');
        if (ext && strcmp(ext, ".txt") == 0) {
            strcpy(ext, ".fits");
        } else {
            strcat(base_path, ".fits");
        }
    } else {
        // Extract sname from filename (substring before last '_')
        // Expected filename format: sname_HH:MM:SS.sss.txt
        char sname[256];
        const char *last_underscore = strrchr(filename, '_');
        if (last_underscore) {
            size_t len = last_underscore - filename;
            if (len >= sizeof(sname)) len = sizeof(sname) - 1;
            strncpy(sname, filename, len);
            sname[len] = '\0';
        } else {
            // Unexpected format, fallback to entire filename without extension?
            // Or assume sname is not present.
            // Let's assume the user provided just filename if no underscore.
            strcpy(sname, filename);
            // remove extension if present
            char *dot = strrchr(sname, '.');
            if (dot) *dot = '\0';
        }

        // Format Date YYYYMMDD from timestamp
        time_t raw_time = (time_t)timestamp;
        struct tm tm_info;
        gmtime_r(&raw_time, &tm_info);
        char date_str[32];
        strftime(date_str, sizeof(date_str), "%Y%m%d", &tm_info);

        // Construct path: teldir/YYYYMMDD/sname/filename_with_fits
        char fname_fits[1024];
        strcpy(fname_fits, filename);
        char *ext = strrchr(fname_fits, '.');
        if (ext && strcmp(ext, ".txt") == 0) {
            strcpy(ext, ".fits");
        } else {
            strcat(fname_fits, ".fits");
        }

        snprintf(base_path, sizeof(base_path), "%s/%s/%s/%s", teldir, date_str, sname, fname_fits);
    }

    // Now check existence
    // 1. Check .fits
    if (access(base_path, F_OK) == 0) {
        strcpy(full_path, base_path);
        return;
    }

    // 2. Check .fits.fz
    char fz_path[1050];
    snprintf(fz_path, sizeof(fz_path), "%s.fz", base_path);
    if (access(fz_path, F_OK) == 0) {
        strcpy(full_path, fz_path);
        return;
    }

    // Default to base_path if neither found (so error message will refer to .fits)
    strcpy(full_path, base_path);
}

void open_input_fits(fitsfile **infptr, const char *path, int *status) {
    fits_open_file(infptr, path, READONLY, status);
    if (*status) return;

    // Check if primary HDU has data. If NAXIS=0, try moving to the first extension.
    // This handles .fits.fz (fpack) where data is often in the 1st extension.
    int naxis = 0;
    fits_get_img_dim(*infptr, &naxis, status);
    if (*status == 0 && naxis == 0) {
        // Move to first extension
        int hdutype;
        fits_movabs_hdu(*infptr, 2, &hdutype, status);
        if (*status) {
            // If error moving, reset status and assume primary was intended but empty
            // fits_report_error(stderr, *status); // Don't report yet
            *status = 0;
            // Go back to primary? No, if move failed, we are likely still at primary.
            // But if move failed, maybe there are no extensions.
            // In that case, naxis=0 means no data.
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <resample.txt> [teldir]\n", argv[0]);
        return 1;
    }

    const char *resample_file = argv[1];
    const char *teldir = NULL;
    if (argc == 3) {
        teldir = argv[2];
    }

    // First pass: scan resample file to find dimensions and max index
    FILE *f = fopen(resample_file, "r");
    if (!f) {
        fprintf(stderr, "Error opening %s\n", resample_file);
        return 1;
    }

    long max_out_idx = -1;
    char first_fits_file_path[1024] = "";
    char line[1024];

    // Format of resample.txt line:
    // Global_index Start_time End_time Source_filename Local_index Resampled_start Resampled_end
    // %d %.6lf %.6lf %s %d %.6lf %.6lf

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;

        int g_idx, l_idx;
        double t_start, t_end, r_start, r_end;
        char fname[1024];

        int n = sscanf(line, "%d %lf %lf %s %d %lf %lf", &g_idx, &t_start, &t_end, fname, &l_idx, &r_start, &r_end);
        if (n < 7) continue;

        if (first_fits_file_path[0] == '\0') {
            // Found first file, compute its path
            get_full_fits_path(first_fits_file_path, teldir, fname, t_start);
        }

        long k_end = (long)floor(r_end - 1e-9);
        if (k_end > max_out_idx) max_out_idx = k_end;
    }
    rewind(f);

    if (max_out_idx < 0) {
        fprintf(stderr, "No valid data found in %s\n", resample_file);
        fclose(f);
        return 1;
    }

    // Open first FITS to get NAXIS
    fitsfile *infptr;
    int status = 0;
    open_input_fits(&infptr, first_fits_file_path, &status);
    if (status) {
        fprintf(stderr, "Error opening first FITS file %s\n", first_fits_file_path);
        fits_report_error(stderr, status);
        return 1;
    }

    int naxis;
    long naxes[3];
    fits_get_img_dim(infptr, &naxis, &status);
    fits_get_img_size(infptr, 3, naxes, &status);
    fits_close_file(infptr, &status);
    error_report(status);

    if (naxis < 2) {
        fprintf(stderr, "Input FITS file must have at least 2 dimensions\n");
        return 1;
    }

    long naxis1 = naxes[0];
    long naxis2 = naxes[1];
    long n_pixels = naxis1 * naxis2;

    printf("Output Dimensions: %ld x %ld x %ld (frames)\n", naxis1, naxis2, max_out_idx + 1);

    // Create Output FITS
    char out_filename[1024];
    // Derive from resample.txt
    strcpy(out_filename, resample_file);
    char *res_ext = strstr(out_filename, ".resample.txt");
    if (res_ext) {
        strcpy(res_ext, ".resample.fits");
    } else {
        strcat(out_filename, ".resample.fits");
    }

    // Delete if exists
    remove(out_filename);

    fitsfile *outfptr;
    fits_create_file(&outfptr, out_filename, &status);
    error_report(status);

    long out_naxes[3] = {naxis1, naxis2, max_out_idx + 1};
    fits_create_img(outfptr, FLOAT_IMG, 3, out_naxes, &status);
    error_report(status);

    // Process
    char current_fits_name[1024] = ""; // This stores the source filename from the text file (e.g. apapane_...txt)
    fitsfile *curr_infptr = NULL;
    float *input_buffer = (float *)malloc(n_pixels * sizeof(float));

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;

        int g_idx, l_idx;
        double t_start, t_end, r_start, r_end;
        char fname[1024];

        int n = sscanf(line, "%d %lf %lf %s %d %lf %lf", &g_idx, &t_start, &t_end, fname, &l_idx, &r_start, &r_end);
        if (n < 7) continue;

        // Check if we need to open a new file
        if (strcmp(fname, current_fits_name) != 0) {
            if (curr_infptr) {
                fits_close_file(curr_infptr, &status);
                status = 0; // ignore close errors?
            }

            char full_path[1024];
            get_full_fits_path(full_path, teldir, fname, t_start);

            open_input_fits(&curr_infptr, full_path, &status);
            if (status) {
                fprintf(stderr, "Warning: Could not open %s. Skipping frame.\n", full_path);
                status = 0;
                curr_infptr = NULL;
                strcpy(current_fits_name, "");
                continue;
            }
            strcpy(current_fits_name, fname);
        }

        if (!curr_infptr) continue;

        // Read input frame
        // l_idx is 0-based in .txt file (assuming mkts output), FITS is 1-based.
        long fpixel[3] = {1, 1, l_idx + 1};

        int anynul;
        fits_read_pix(curr_infptr, TFLOAT, fpixel, n_pixels, NULL, input_buffer, &anynul, &status);
        if (status) {
            fprintf(stderr, "Error reading frame %d from %s\n", l_idx, current_fits_name);
            status = 0; // Try to continue?
            continue;
        }

        // Distribute to output frames
        long k_start = (long)floor(r_start);
        long k_end = (long)floor(r_end - 1e-9);

        // Before adding, flush any old frames from buffer
        flush_frames(outfptr, k_start, naxis1, naxis2);

        for (long k = k_start; k <= k_end; k++) {
            // Calculate overlap
            double o_start = fmax(r_start, (double)k);
            double o_end = fmin(r_end, (double)(k + 1));
            double overlap = o_end - o_start;

            if (overlap <= 0) continue;

            float *out_data = get_output_frame(k, n_pixels);

            // Add weighted input
            for (long p = 0; p < n_pixels; p++) {
                out_data[p] += input_buffer[p] * (float)overlap;
            }
        }
    }

    // Flush remaining
    flush_all_frames(outfptr, naxis1, naxis2);

    if (curr_infptr) fits_close_file(curr_infptr, &status);
    fits_close_file(outfptr, &status);

    fclose(f);
    free(input_buffer);

    return 0;
}
