/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

#define BLOCK_SIZE 2048
#define IO_BUFFER_SIZE (64 * 1024)

#if defined(_WIN32)
    #include <windows.h>
    #include <direct.h>
    #define MKDIR(path) _mkdir(path)
    #define STRICMP _stricmp
    #define PATH_SEP '\\'
    #define PATH_SEP_STR "\\"
    #ifndef PATH_MAX
        #define PATH_MAX MAX_PATH
    #endif
#else
    #include <dirent.h>
    #include <strings.h>
    #include <unistd.h>
    #define MKDIR(path) mkdir(path, 0777)
    #define STRICMP strcasecmp
    #define PATH_SEP '/'
    #define PATH_SEP_STR "/"
    #ifndef PATH_MAX
        #define PATH_MAX 4096
    #endif
#endif

#pragma pack(push, 1)
typedef struct {
    uint32_t offset;
    uint16_t streamingSize;
    uint16_t sizeInArchive;
    char name[24];
} IMGEntry;
#pragma pack(pop)

typedef struct {
    char name[PATH_MAX];
    char full_path[PATH_MAX];
    uint32_t file_size;
    char ext[16];
} FileInfo;

typedef struct {
    IMGEntry entry;
    uint32_t original_index;
} RebuildEntry;

static const char* VALID_EXTENSIONS[] = {
    ".dff", ".txd", ".rrr", ".ifp", ".col", ".ipl", ".dat", ".scm", ".cut", ".huff"
};
static const size_t VALID_EXT_COUNT = sizeof(VALID_EXTENSIONS) / sizeof(VALID_EXTENSIONS[0]);

void ensure_dir_exists(const char* dir) {
    MKDIR(dir);
}

int is_directory(const char* path) {
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) return 0;
    return S_ISDIR(statbuf.st_mode);
#endif
}

void get_extension(const char* filename, char* out_ext, size_t max_len) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        out_ext[0] = '\0';
    } else {
        snprintf(out_ext, max_len, "%s", dot);
    }
}

int is_valid_game_extension(const char* ext) {
    for (size_t i = 0; i < VALID_EXT_COUNT; i++) {
        if (STRICMP(ext, VALID_EXTENSIONS[i]) == 0) return 1;
    }
    return 0;
}

int compare_by_extension(const void* a, const void* b) {
    const FileInfo* fileA = (const FileInfo*)a;
    const FileInfo* fileB = (const FileInfo*)b;
    int ext_cmp = STRICMP(fileA->ext, fileB->ext);
    if (ext_cmp != 0) return ext_cmp;
    return STRICMP(fileA->name, fileB->name);
}

int compare_entries_by_extension(const void* a, const void* b) {
    const RebuildEntry* entryA = (const RebuildEntry*)a;
    const RebuildEntry* entryB = (const RebuildEntry*)b;
    
    char nameA[25] = {0}, nameB[25] = {0};
    memcpy(nameA, entryA->entry.name, 24);
    memcpy(nameB, entryB->entry.name, 24);

    char extA[16], extB[16];
    get_extension(nameA, extA, sizeof(extA));
    get_extension(nameB, extB, sizeof(extB));
    int ext_cmp = STRICMP(extA, extB);
    if (ext_cmp != 0) return ext_cmp;
    return STRICMP(nameA, nameB);
}

int compare_entries_by_offset(const void* a, const void* b) {
    const IMGEntry* entryA = (const IMGEntry*)a;
    const IMGEntry* entryB = (const IMGEntry*)b;
    if (entryA->offset < entryB->offset) return -1;
    if (entryA->offset > entryB->offset) return 1;
    return 0;
}

void get_filename_no_ext(const char* filepath, char* out_name, size_t max_len) {
    const char* slash1 = strrchr(filepath, '\\');
    const char* slash2 = strrchr(filepath, '/');
    const char* slash = (slash1 > slash2) ? slash1 : slash2;
    
    const char* base = (slash) ? slash + 1 : filepath;
    snprintf(out_name, max_len, "%s", base);

    char* dot = strrchr(out_name, '.');
    if (dot) *dot = '\0';
}

void extract_single_img(const char* img_path, const char* out_dir, int verbose) {
    FILE* f = fopen(img_path, "rb");
    if (!f) { printf("[ERROR] Failed to open: %s\n", img_path); return; }

    char sig[4];
    if (fread(sig, 1, 4, f) != 4 || strncmp(sig, "VER2", 4) != 0) {
        printf("[SKIP] Not an IMG v2: %s\n", img_path);
        fclose(f); return;
    }

    uint32_t entry_count = 0;
    if (fread(&entry_count, 4, 1, f) != 1) {
        fclose(f); return;
    }
    printf("--> Extracting %s (%u files) -> %s\n", img_path, entry_count, out_dir);

    IMGEntry* entries = (IMGEntry*)malloc(entry_count * sizeof(IMGEntry));
    if (!entries) { fclose(f); return; }
    if (fread(entries, sizeof(IMGEntry), entry_count, f) != entry_count) {
        free(entries);
        fclose(f);
        return;
    }

    ensure_dir_exists(out_dir);

    uint8_t* buffer = (uint8_t*)malloc(IO_BUFFER_SIZE);
    if (!buffer) {
        free(entries);
        fclose(f);
        return;
    }

    for (uint32_t i = 0; i < entry_count; i++) {
        char file_name[25] = {0};
        memcpy(file_name, entries[i].name, 24);

        char out_path[PATH_MAX];
        snprintf(out_path, sizeof(out_path), "%s" PATH_SEP_STR "%s", out_dir, file_name);

        FILE* out = fopen(out_path, "wb");
        if (out) {
            fseek(f, (long)entries[i].offset * BLOCK_SIZE, SEEK_SET);
            uint32_t bytes_left = (uint32_t)entries[i].streamingSize * BLOCK_SIZE;
            while (bytes_left > 0) {
                uint32_t chunk = (bytes_left > IO_BUFFER_SIZE) ? IO_BUFFER_SIZE : bytes_left;
                size_t read_bytes = fread(buffer, 1, chunk, f);
                if (read_bytes == 0) break;
                fwrite(buffer, 1, read_bytes, out);
                bytes_left -= (uint32_t)read_bytes;
            }
            fclose(out);
            if (verbose) {
                printf("  [EXTRACTED] %s (%u sectors)\n", file_name, entries[i].streamingSize);
            }
        }
    }
    free(buffer);
    free(entries);
    fclose(f);
}

void add_file_to_pack_list(const char* in_dir, const char* filename, uint32_t file_size, FileInfo** files, uint32_t* file_count, uint32_t* capacity, uint32_t* ignored_count, int allow_all, int verbose) {
    char ext[16];
    get_extension(filename, ext, sizeof(ext));

    if (allow_all || is_valid_game_extension(ext)) {
        if (*file_count >= *capacity) {
            *capacity = (*capacity == 0) ? 1024 : (*capacity * 2);
            FileInfo* new_files = (FileInfo*)realloc(*files, *capacity * sizeof(FileInfo));
            if (!new_files) return;
            *files = new_files;
        }
        snprintf((*files)[*file_count].name, PATH_MAX, "%s", filename);
        snprintf((*files)[*file_count].full_path, PATH_MAX, "%s" PATH_SEP_STR "%s", in_dir, filename);
        (*files)[*file_count].file_size = file_size;
        snprintf((*files)[*file_count].ext, sizeof((*files)[*file_count].ext), "%s", ext);
        (*file_count)++;
    } else {
        (*ignored_count)++;
        if (verbose) {
            printf("  [IGNORED] Skipped non-game file: %s\n", filename);
        }
    }
}


void pack_single_folder(const char* in_dir, const char* out_img, int allow_all, int verbose) {
    uint32_t capacity = 1024;
    uint32_t file_count = 0;
    uint32_t ignored_count = 0;
    FileInfo* files = (FileInfo*)malloc(capacity * sizeof(FileInfo));
    if (!files) {
        printf("[ERROR] Memory allocation failed for files list!\n");
        return;
    }

#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    char search_path[PATH_MAX];
    snprintf(search_path, sizeof(search_path), "%s\\*.*", in_dir);

    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                add_file_to_pack_list(in_dir, fd.cFileName, fd.nFileSizeLow, &files, &file_count, &capacity, &ignored_count, allow_all, verbose);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    } else {
        printf("[ERROR] Failed to read directory: %s\n", in_dir); free(files); return;
    }
#else
    DIR* dir = opendir(in_dir);
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", in_dir, ent->d_name);
            
            struct stat statbuf;
            if (stat(full_path, &statbuf) == 0 && !S_ISDIR(statbuf.st_mode)) {
                add_file_to_pack_list(in_dir, ent->d_name, (uint32_t)statbuf.st_size, &files, &file_count, &capacity, &ignored_count, allow_all, verbose);
            }
        }
        closedir(dir);
    } else {
        printf("[ERROR] Failed to read directory: %s\n", in_dir); free(files); return;
    }
#endif

    if (ignored_count > 0 && !verbose) {
        printf("  (Skipped %u unsupported files. Add -v for details or -a to pack them all)\n", ignored_count);
    }

    if (file_count == 0) { 
        printf("[SKIP] Directory %s is empty or contains no game files!\n", in_dir); 
        free(files); return; 
    }

    if (verbose) printf("  [SORT] Sorting %u files by extension...\n", file_count);
    qsort(files, file_count, sizeof(FileInfo), compare_by_extension);

    FILE* out = fopen(out_img, "wb");
    if (!out) { printf("[ERROR] Creating %s\n", out_img); free(files); return; }

    printf("--> Packing %s (%u files) -> %s\n", in_dir, file_count, out_img);

    uint32_t header_bytes = 8 + (file_count * sizeof(IMGEntry));
    uint32_t header_blocks = (header_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    IMGEntry* entries = (IMGEntry*)calloc(file_count, sizeof(IMGEntry));
    if (!entries) { fclose(out); free(files); return; }

    uint32_t current_block_offset = header_blocks;

    for (uint32_t i = 0; i < file_count; i++) {
        uint32_t size_in_blocks = (files[i].file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        entries[i].offset = current_block_offset;
        entries[i].streamingSize = size_in_blocks;
        entries[i].sizeInArchive = 0;
        memset(entries[i].name, 0, 24);
        strncpy(entries[i].name, files[i].name, 23);
        current_block_offset += size_in_blocks;
    }

    fwrite("VER2", 1, 4, out);
    fwrite(&file_count, 4, 1, out);
    fwrite(entries, sizeof(IMGEntry), file_count, out);

    uint32_t header_padding = (header_blocks * BLOCK_SIZE) - header_bytes;
    if (header_padding > 0) {
        uint8_t* pad = (uint8_t*)calloc(1, header_padding);
        if (pad) {
            fwrite(pad, 1, header_padding, out);
            free(pad);
        }
    }

    uint8_t zero_pad[BLOCK_SIZE] = {0};
    uint8_t* buffer = (uint8_t*)malloc(IO_BUFFER_SIZE);
    if (!buffer) {
        free(entries);
        free(files);
        fclose(out);
        return;
    }

    for (uint32_t i = 0; i < file_count; i++) {
        FILE* in = fopen(files[i].full_path, "rb");
        if (!in) continue;

        size_t bytesRead;
        while ((bytesRead = fread(buffer, 1, IO_BUFFER_SIZE, in)) > 0) {
            fwrite(buffer, 1, bytesRead, out);
        }
        fclose(in);

        uint32_t remainder = files[i].file_size % BLOCK_SIZE;
        uint32_t pad_size = 0;
        if (remainder != 0) {
            pad_size = BLOCK_SIZE - remainder;
            fwrite(zero_pad, 1, pad_size, out);
        }

        if (verbose) {
            printf("  [PACKED] %-24s | Size: %8u bytes | Padding: %4u bytes\n", 
                   files[i].name, files[i].file_size, pad_size);
        }
    }

    free(buffer);
    free(entries);
    free(files);
    fclose(out);
}

void rebuild_single_img_safe(const char* in_img, int keep_bak, int verbose) {
    FILE* f_in = fopen(in_img, "rb");
    if (!f_in) { printf("[ERROR] Failed to open: %s\n", in_img); return; }

    char sig[4];
    if (fread(sig, 1, 4, f_in) != 4 || strncmp(sig, "VER2", 4) != 0) {
        printf("[SKIP] Not an IMG v2: %s\n", in_img);
        fclose(f_in); return;
    }

    uint32_t entry_count = 0;
    if (fread(&entry_count, 4, 1, f_in) != 1) {
        fclose(f_in); return;
    }

    IMGEntry* old_entries = (IMGEntry*)malloc(entry_count * sizeof(IMGEntry));
    if (!old_entries) { fclose(f_in); return; }
    if (fread(old_entries, sizeof(IMGEntry), entry_count, f_in) != entry_count) {
        free(old_entries);
        fclose(f_in);
        return;
    }

    RebuildEntry* rebuild_entries = (RebuildEntry*)malloc(entry_count * sizeof(RebuildEntry));
    if (!rebuild_entries) { free(old_entries); fclose(f_in); return; }

    for (uint32_t i = 0; i < entry_count; i++) {
        rebuild_entries[i].entry = old_entries[i];
        rebuild_entries[i].original_index = i;
    }

    if (verbose) printf("  [SORT] Sorting %u entries for rebuild...\n", entry_count);
    qsort(rebuild_entries, entry_count, sizeof(RebuildEntry), compare_entries_by_extension);

    uint32_t header_bytes = 8 + (entry_count * sizeof(IMGEntry));
    uint32_t header_blocks = (header_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint32_t current_block_offset = header_blocks;

    IMGEntry* sorted_entries = (IMGEntry*)malloc(entry_count * sizeof(IMGEntry));
    if (!sorted_entries) { free(old_entries); free(rebuild_entries); fclose(f_in); return; }

    for (uint32_t i = 0; i < entry_count; i++) {
        rebuild_entries[i].entry.offset = current_block_offset;
        current_block_offset += rebuild_entries[i].entry.streamingSize;
        sorted_entries[i] = rebuild_entries[i].entry;
    }

    char tmp_img[PATH_MAX];
    snprintf(tmp_img, sizeof(tmp_img), "%s.tmp", in_img);

    FILE* f_out = fopen(tmp_img, "wb");
    if (!f_out) {
        printf("[ERROR] Failed to create rebuild file: %s\n", tmp_img);
        free(old_entries); free(rebuild_entries); free(sorted_entries); fclose(f_in); return;
    }

    printf("--> Rebuilding archive %s (%u files)\n", in_img, entry_count);

    fwrite("VER2", 1, 4, f_out);
    fwrite(&entry_count, 4, 1, f_out);
    fwrite(sorted_entries, sizeof(IMGEntry), entry_count, f_out);

    uint32_t header_padding = (header_blocks * BLOCK_SIZE) - header_bytes;
    if (header_padding > 0) {
        uint8_t* pad = (uint8_t*)calloc(1, header_padding);
        if (pad) {
            fwrite(pad, 1, header_padding, f_out);
            free(pad);
        }
    }

    uint8_t* buffer = (uint8_t*)malloc(IO_BUFFER_SIZE);
    if (!buffer) {
        free(old_entries); free(rebuild_entries); free(sorted_entries); fclose(f_in); fclose(f_out); return;
    }

    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t orig_idx = rebuild_entries[i].original_index;
        uint32_t old_offset = old_entries[orig_idx].offset;
        uint16_t old_streaming_size = old_entries[orig_idx].streamingSize;

        fseek(f_in, (long)old_offset * BLOCK_SIZE, SEEK_SET);
        uint32_t bytes_left = (uint32_t)old_streaming_size * BLOCK_SIZE;
        while (bytes_left > 0) {
            uint32_t chunk = (bytes_left > IO_BUFFER_SIZE) ? IO_BUFFER_SIZE : bytes_left;
            size_t read_bytes = fread(buffer, 1, chunk, f_in);
            if (read_bytes == 0) break;
            fwrite(buffer, 1, read_bytes, f_out);
            bytes_left -= (uint32_t)read_bytes;
        }

        if (verbose) {
            char file_name[25] = {0};
            memcpy(file_name, sorted_entries[i].name, 24);
            printf("  [REBUILT] %-24s | Sectors: %4u\n", file_name, old_streaming_size);
        }
    }

    free(buffer);
    free(old_entries);
    free(rebuild_entries);
    free(sorted_entries);
    fclose(f_in);
    fclose(f_out);

    if (keep_bak) {
        char bak_img[PATH_MAX];
        snprintf(bak_img, sizeof(bak_img), "%s.bak", in_img);
        remove(bak_img);
        rename(in_img, bak_img);
    } else {
        remove(in_img);
    }
    rename(tmp_img, in_img);
}

void verify_single_img(const char* img_path, int verbose) {
    FILE* f = fopen(img_path, "rb");
    if (!f) { printf("[ERROR] Failed to open: %s\n", img_path); return; }

    char sig[4];
    if (fread(sig, 1, 4, f) != 4 || strncmp(sig, "VER2", 4) != 0) {
        if (verbose) printf("[SKIP] Not an IMG v2: %s\n", img_path);
        fclose(f); return;
    }

    uint32_t entry_count = 0;
    if (fread(&entry_count, 4, 1, f) != 1) {
        fclose(f); return;
    }
    
    IMGEntry* entries = (IMGEntry*)malloc(entry_count * sizeof(IMGEntry));
    if (!entries) { fclose(f); return; }
    if (fread(entries, sizeof(IMGEntry), entry_count, f) != entry_count) {
        free(entries);
        fclose(f);
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fclose(f);

    IMGEntry* sorted_entries = (IMGEntry*)malloc(entry_count * sizeof(IMGEntry));
    if (!sorted_entries) { free(entries); return; }
    memcpy(sorted_entries, entries, entry_count * sizeof(IMGEntry));
    qsort(sorted_entries, entry_count, sizeof(IMGEntry), compare_entries_by_offset);

    int overlaps = 0;
    int holes = 0;
    int out_of_bounds = 0;

    for (uint32_t i = 0; i < entry_count; i++) {
        char file_name[25] = {0};
        memcpy(file_name, sorted_entries[i].name, 24);

        long end_pos = (long)(sorted_entries[i].offset + sorted_entries[i].streamingSize) * BLOCK_SIZE;
        if (end_pos > file_size) {
            out_of_bounds++;
            if (verbose) printf("  [ERROR] File %s exceeds physical archive bounds!\n", file_name);
        }

        if (i < entry_count - 1) {
            uint32_t current_end = sorted_entries[i].offset + sorted_entries[i].streamingSize;
            uint32_t next_start = sorted_entries[i+1].offset;

            if (current_end > next_start) {
                overlaps++;
                if (verbose) printf("  [OVERLAP] Conflict between %s and next entry\n", file_name);
            } else if (current_end < next_start) {
                holes++;
                if (verbose) printf("  [HOLE] Empty space after %s (%u sectors)\n", file_name, next_start - current_end);
            }
        }
    }

    if (overlaps > 0 || holes > 0 || out_of_bounds > 0) {
        printf("[VERIFY] Archive %s has issues:\n", img_path);
        printf("  Sector overlaps: %d\n", overlaps);
        printf("  Empty 'holes' (fragmentation): %d\n", holes);
        printf("  Out of bounds files: %d\n", out_of_bounds);
        printf("  => RECOMMENDED: Use the -rebuild command for automatic fixing.\n");
    } else {
        printf("[VERIFY] Archive %s is in perfect condition! No issues found.\n", img_path);
    }

    free(entries);
    free(sorted_entries);
}

void process_extract(const char* img_param, const char* folder_param, int verbose) {
    if (is_directory(img_param)) {
        printf("[BATCH] Searching for .img in directory: %s\n", img_param);
        ensure_dir_exists(folder_param);

#if defined(_WIN32)
        WIN32_FIND_DATAA fd;
        char search_path[PATH_MAX];
        snprintf(search_path, sizeof(search_path), "%s\\*.img", img_param);

        HANDLE hFind = FindFirstFileA(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    char full_img_path[PATH_MAX];
                    snprintf(full_img_path, sizeof(full_img_path), "%s\\%s", img_param, fd.cFileName);

                    char name_no_ext[PATH_MAX];
                    get_filename_no_ext(fd.cFileName, name_no_ext, sizeof(name_no_ext));

                    char subfolder_out[PATH_MAX * 2];
                    snprintf(subfolder_out, sizeof(subfolder_out), "%s" PATH_SEP_STR "%s", folder_param, name_no_ext);

                    extract_single_img(full_img_path, subfolder_out, verbose);
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        } else {
            printf("No .img files found in %s\n", img_param);
        }
#else
        DIR* dir = opendir(img_param);
        int found = 0;
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                char ext[16];
                get_extension(ent->d_name, ext, sizeof(ext));
                if (STRICMP(ext, ".img") == 0) {
                    found = 1;
                    char full_img_path[PATH_MAX];
                    snprintf(full_img_path, sizeof(full_img_path), "%s/%s", img_param, ent->d_name);

                    char name_no_ext[PATH_MAX];
                    get_filename_no_ext(ent->d_name, name_no_ext, sizeof(name_no_ext));

                    char subfolder_out[PATH_MAX * 2];
                    snprintf(subfolder_out, sizeof(subfolder_out), "%s" PATH_SEP_STR "%s", folder_param, name_no_ext);

                    extract_single_img(full_img_path, subfolder_out, verbose);
                }
            }
            closedir(dir);
        }
        if (!found) {
            printf("No .img files found in %s\n", img_param);
        }
#endif
    } else {
        extract_single_img(img_param, folder_param, verbose);
    }
    printf("[DONE] Extraction completed!\n");
}

void process_pack(const char* img_param, const char* folder_param, int allow_all, int verbose) {
    int has_subdirs = 0;

#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    char search_path[PATH_MAX];
    snprintf(search_path, sizeof(search_path), "%s\\*.*", folder_param);

    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                has_subdirs = 1;
                break;
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(folder_param);
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s/%s", folder_param, ent->d_name);
                if (is_directory(full_path)) {
                    has_subdirs = 1;
                    break;
                }
            }
        }
        closedir(dir);
    }
#endif

    if (has_subdirs) {
        printf("[BATCH] Packing subdirectories from %s -> %s\n", folder_param, img_param);
        ensure_dir_exists(img_param);

#if defined(_WIN32)
        hFind = FindFirstFileA(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                    
                    char subfolder_in[PATH_MAX];
                    snprintf(subfolder_in, sizeof(subfolder_in), "%s\\%s", folder_param, fd.cFileName);

                    char out_img_path[PATH_MAX];
                    snprintf(out_img_path, sizeof(out_img_path), "%s\\%s.img", img_param, fd.cFileName);

                    pack_single_folder(subfolder_in, out_img_path, allow_all, verbose);
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
#else
        dir = opendir(folder_param);
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                    char full_path[PATH_MAX];
                    snprintf(full_path, sizeof(full_path), "%s/%s", folder_param, ent->d_name);
                    
                    if (is_directory(full_path)) {
                        char subfolder_in[PATH_MAX];
                        snprintf(subfolder_in, sizeof(subfolder_in), "%s/%s", folder_param, ent->d_name);

                        char out_img_path[PATH_MAX];
                        snprintf(out_img_path, sizeof(out_img_path), "%s/%s.img", ent->d_name, ent->d_name);

                        pack_single_folder(subfolder_in, out_img_path, allow_all, verbose);
                    }
                }
            }
            closedir(dir);
        }
#endif
    } else {
        char final_img_path[PATH_MAX * 2];
        snprintf(final_img_path, sizeof(final_img_path), "%s", img_param);

        if (is_directory(img_param)) {
            char folder_name[PATH_MAX];
            get_filename_no_ext(folder_param, folder_name, sizeof(folder_name));
            snprintf(final_img_path, sizeof(final_img_path), "%s" PATH_SEP_STR "%s.img", img_param, folder_name);
        }

        pack_single_folder(folder_param, final_img_path, allow_all, verbose);
    }
    printf("[DONE] Packing completed!\n");
}

void process_rebuild(const char* img_param, int keep_bak, int verbose) {
    if (is_directory(img_param)) {
        printf("[BATCH] Searching for .img in directory: %s\n", img_param);

#if defined(_WIN32)
        WIN32_FIND_DATAA fd;
        char search_path[PATH_MAX];
        snprintf(search_path, sizeof(search_path), "%s\\*.img", img_param);

        HANDLE hFind = FindFirstFileA(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    char full_img_path[PATH_MAX];
                    snprintf(full_img_path, sizeof(full_img_path), "%s\\%s", img_param, fd.cFileName);

                    rebuild_single_img_safe(full_img_path, keep_bak, verbose);
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        } else {
            printf("No .img files found in %s\n", img_param);
        }
#else
        DIR* dir = opendir(img_param);
        int found = 0;
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                char ext[16];
                get_extension(ent->d_name, ext, sizeof(ext));
                if (STRICMP(ext, ".img") == 0) {
                    found = 1;
                    char full_img_path[PATH_MAX];
                    snprintf(full_img_path, sizeof(full_img_path), "%s/%s", img_param, ent->d_name);

                    rebuild_single_img_safe(full_img_path, keep_bak, verbose);
                }
            }
            closedir(dir);
        }
        if (!found) {
            printf("No .img files found in %s\n", img_param);
        }
#endif
    } else {
        rebuild_single_img_safe(img_param, keep_bak, verbose);
    }
    printf("[DONE] Rebuild completed!\n");
}

char* parse_path(int* i, int argc, char* argv[]) {
    char buffer[PATH_MAX] = {0};
    (*i)++;
    if (*i < argc) {
        snprintf(buffer, sizeof(buffer), "%s", argv[*i]);
        while (*i + 1 < argc && argv[*i + 1][0] != '-') {
            (*i)++;
            size_t current_len = strlen(buffer);
            if (current_len + 1 + strlen(argv[*i]) < sizeof(buffer)) {
                strcat(buffer, " ");
                strcat(buffer, argv[*i]);
            }
        }
        size_t len = strlen(buffer);
        if (len >= 2 && buffer[0] == '"' && buffer[len-1] == '"') {
            buffer[len-1] = '\0';
            memmove(buffer, buffer+1, len);
        }
        char* res = (char*)malloc(strlen(buffer) + 1);
        if (res) strcpy(res, buffer);
        return res;
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    char* mode = NULL;
    char* img_path = NULL;
    char* folder_path = NULL;
    int allow_all = 0;
    int verbose = 0;
    int keep_bak = 0;

    for (int i = 1; i < argc; i++) {
        if (STRICMP(argv[i], "-extract") == 0) {
            mode = "extract";
        } else if (STRICMP(argv[i], "-pack") == 0) {
            mode = "pack";
        } else if (STRICMP(argv[i], "-verify") == 0) {
            mode = "verify";
        } else if (STRICMP(argv[i], "-rebuild") == 0) {
            mode = "rebuild";
        } else if (STRICMP(argv[i], "-img") == 0 && i + 1 < argc) {
            img_path = parse_path(&i, argc, argv);
        } else if (STRICMP(argv[i], "-folder") == 0 && i + 1 < argc) {
            folder_path = parse_path(&i, argc, argv);
        } else if (STRICMP(argv[i], "-allow-all") == 0 || STRICMP(argv[i], "-a") == 0) {
            allow_all = 1;
        } else if (STRICMP(argv[i], "-verbose") == 0 || STRICMP(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (STRICMP(argv[i], "-bak") == 0 || STRICMP(argv[i], "-b") == 0) {
            keep_bak = 1;
        }
    }

    if (!mode || !img_path) {
        printf("Usage:\n");
        printf("  Modes:\n");
        printf("    -extract                Extract mode\n");
        printf("    -pack                   Pack mode\n");
        printf("    -verify                 Verify archive integrity\n");
        printf("    -rebuild                Rebuild archive(s) in-place\n\n");
        printf("  Parameters:\n");
        printf("    -img <path>             Path to .img file or folder with .img files\n");
        printf("    -folder <path>          Path to folder with files (for extract/pack)\n");
        printf("    -bak, -b                Save original file as .bak during rebuild\n");
        printf("    -v, --verbose           Detailed operation log for each file\n");
        printf("    -a, --allow-all         Pack absolutely all files (ignore extension filter)\n");
        printf("\n");
        printf("  Examples:\n");
        #if defined(_WIN32)
        printf("    imgtool -extract -img C:\\gta\\gta3.img -folder C:\\unpacked -v\n");
        printf("    imgtool -verify -img C:\\gta\\gta3.img\n");
        printf("    imgtool -rebuild -img C:\\gta\\gta3.img -bak -v\n");
        printf("    imgtool -rebuild -img \"C:\\gta\" -v\n");
    #else
        printf("    imgtool -extract -img /home/user/gta/gta3.img -folder /home/user/unpacked -v\n");
        printf("    imgtool -verify -img /home/user/gta/gta3.img\n");
        printf("    imgtool -rebuild -img /home/user/gta/gta3.img -bak -v\n");
        printf("    imgtool -rebuild -img \"/home/user/gta\" -v\n");
    #endif
        
        if (img_path) free(img_path);
        if (folder_path) free(folder_path);
        return 1;
    }

    int exit_code = 0;

    if (strcmp(mode, "extract") == 0) {
        if (!folder_path) { printf("[ERROR] For -extract you must specify -folder\n"); exit_code = 1; }
        else { process_extract(img_path, folder_path, verbose); }
    } else if (strcmp(mode, "pack") == 0) {
        if (!folder_path) { printf("[ERROR] For -pack you must specify -folder\n"); exit_code = 1; }
        else { process_pack(img_path, folder_path, allow_all, verbose); }
    } else if (strcmp(mode, "verify") == 0) {
        verify_single_img(img_path, verbose);
    } else if (strcmp(mode, "rebuild") == 0) {
        process_rebuild(img_path, keep_bak, verbose);
    }

    if (img_path) free(img_path);
    if (folder_path) free(folder_path);

    return exit_code;
}
