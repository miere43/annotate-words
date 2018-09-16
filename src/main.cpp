#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <assert.h>
#include <strsafe.h>
#define JSON_IMPLEMENTATION
#include "json.h"
#include "sqlite3.h"

// Settings!
const char* annotate_filename = R"(D:\Vlad\learn\japanese\japanese.txt)";                   // Full path to the file that will be annotated.
const char* annotate_result_filename = R"(D:\Vlad\learn\japanese\japanese.annotated.txt)";  // Where to place resulting file with annotations, overwrites existing file if any.

const char* collection_filename = R"(C:\Users\Vladislav\AppData\Roaming\Anki2\User 1\collection.anki2)";  // Path to Anki 2 collection.
const char* collection_model_name = "Japanese Vocab";         // Name of model to lookup stuff.
const char* collection_primary_field_name = "Word";           // Name of primary field that contains word that can be looked up.
const char* collection_annotation_field_name = "Recording";   // Annotation that will be prepended to words containing primary field.

const bool  drop_lines_without_word = true;  // Do not store store lines without words to annotate in memory and don't write them back to annotated file.
const bool  sort_output_lines = true;        // Apply sorting procedure to lines just before writing them to disk (look at sort_lines())

// Look at different implementations near procedure write_annotations()
#define write_annotations_impl write_annotations_v3

// Determines amount of reserved virtual memory for internal buffers.
enum { MAX_NOTES = 0x16000 };  // Maximum amount of notes that can be loaded from Anki.
enum { MAX_LINES = 0x16000 };  // Maximum amount of lines that can be processed from source file.
enum { MAX_CHARACTER_BUFFER_SIZE = 0x96000 };  // Maximum amount of characters in character buffer.

// Not settings anymore.

#ifdef NDEBUG
#define verify(expr)  do { if (!(expr)) { MessageBoxA(0, "Assertion failed: " #expr "\n\nProgram will be terminated.", "Assertion failed", MB_ICONERROR | MB_OK); ExitProcess(1); } } while (0)
#else
#define verify assert
#endif

char* read_file(const char* filename, int* filesize) {
    assert(filename);

    HANDLE file = CreateFileA(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    verify(file != INVALID_HANDLE_VALUE);

    LARGE_INTEGER filesize_large;
    verify(GetFileSizeEx(file, &filesize_large));
    verify(filesize_large.LowPart > 0);
    verify(filesize_large.LowPart <= INT_MAX);

    char* data = (char*)::malloc(filesize_large.LowPart + 1);
    verify(data);

    DWORD nread = 0;
    verify(ReadFile(file, data, filesize_large.LowPart, &nread, NULL));
    verify((int)nread == filesize_large.LowPart);
    verify(CloseHandle(file));

    if (filesize)  *filesize = (int)filesize_large.LowPart;
    data[filesize_large.LowPart] = '\0';
    return data;
}

HANDLE create_file(const char* filename) {
    assert(filename);

    HANDLE file = CreateFileA(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    verify(file != INVALID_HANDLE_VALUE);

    return file;
}

void write_to_file(HANDLE file, const void* buffer, int buffer_size) {
    assert(file != INVALID_HANDLE_VALUE);
    assert(buffer_size >= 0);

    DWORD written;
    verify(WriteFile(file, buffer, buffer_size, &written, NULL));
    verify(written == (DWORD)buffer_size);
}

inline void close_file(HANDLE file) {
    verify(CloseHandle(file));
}

char* next_line(char** source_text, char** line_end) {
    assert(source_text);
    assert(*source_text);
    assert(line_end);

    char* source = *source_text;
    char* line = source;
    
    do {
        char c = source[0];
        if (c == '\0') {
            *line_end = source;
            *source_text = source;
            return source != line ? line : NULL;
        }
        if (c == '\n') {
            *line_end = source;
            *source_text = source + 1;
            return line;
        }
        if (c == '\r' && source[1] == '\n') {
            *line_end = source;
            *source_text = source + 2;
            return line;
        }
        ++source;
    } while (true);

    return line;
}

enum { UNICODE_EOF = 0, UNICODE_INVALID_CHARACTER = 0xFFFF };
inline int read_utf8_codepoint(char** source, int length) {
    if (length == 0)
        return UNICODE_EOF;
    unsigned char* s = *(unsigned char**)source;
    int c = *s;
    if (c <= 0x7F) {
        *source += 1;
        return c;
    } else if (c <= 0xDF) {
        if (length < 2)
            return UNICODE_INVALID_CHARACTER;
        if ((s[0] & 0b11100000) != 0b11000000)
            return UNICODE_INVALID_CHARACTER;
        if ((s[1] & 0b11000000) != 0b10000000)
            return UNICODE_INVALID_CHARACTER;
        int v = (s[1] & 0x3F) | ((s[0] & 0x1F) << 6);
        *source += 2;
        return v;
    } else if (c <= 0xEF) {
        if (length < 3)
            return UNICODE_INVALID_CHARACTER;
        if ((s[0] & 0b11110000) != 0b11100000)
            return UNICODE_INVALID_CHARACTER;
        if ((s[1] & 0b11000000) != 0b10000000)
            return UNICODE_INVALID_CHARACTER;
        if ((s[2] & 0b11000000) != 0b10000000)
            return UNICODE_INVALID_CHARACTER;
        int v = ((s[0] & 0b00001111) << 12) | ((s[1] & 0b00111111) << 6) | (s[2] & 0b00111111);
        *source += 3;
        return v;
    } else if (c <= 0xF7) {
        if (length < 4)
            return UNICODE_INVALID_CHARACTER;
        if ((s[0] & 0b11111000) != 0b11110000)
            return UNICODE_INVALID_CHARACTER;
        if ((s[1] & 0b11000000) != 0b10000000)
            return UNICODE_INVALID_CHARACTER;
        if ((s[2] & 0b11000000) != 0b10000000)
            return UNICODE_INVALID_CHARACTER;
        if ((s[3] & 0b11000000) != 0b10000000)
            return UNICODE_INVALID_CHARACTER;
        int v = ((s[0] & 0b00000111) << 18) | ((s[1] & 0b00111111) << 12) | ((s[2] & 0b00111111) << 6) | (s[3] & 0b00111111);
        *source += 4;
        return v;
    }
    return UNICODE_INVALID_CHARACTER;
}

inline bool is_cjk_codepoint(int c) {
    return
        (c >= 0x4E00 && c <= 0x9FFF)   ||  // CJK Unified Ideographs
        (c >= 0x3400 && c <= 0x4DBF)   ||  // CJK Unified Ideographs Extension A
        (c >= 0x20000 && c <= 0x2A6DF) ||  // CJK Unified Ideographs Extension B
        (c >= 0x2A700 && c <= 0x2B73F) ||  // CJK Unified Ideographs Extension C
        (c >= 0x2B740 && c <= 0x2B81F) ||  // CJK Unified Ideographs Extension D
        (c >= 0x2B820 && c <= 0x2CEAF) ||  // CJK Unified Ideographs Extension E
        (c >= 0x2CEB0 && c <= 0x2EBEF);    // CJK Unified Ideographs Extension F
}

inline bool is_kana_codepoint(int c) {
    return
        (c >= 0x3040 && c <= 0x309F) ||    // Hiragana
        (c >= 0x30A0 && c <= 0x30FF);      // Katakana, Katakana Phonetic Extensions, Halfwidth Katakana
}

inline bool is_space_codepoint(int c) {
    return
        c == ' ' ||
        c == '\t';
}

struct ResultLine {
    char* line = NULL;
    char* line_end = NULL;
    char* word = NULL;
    char* word_end = NULL;
};

static ResultLine result_lines[MAX_LINES];
static int result_lines_count = 0;

ResultLine* new_result_line() {
    verify(result_lines_count <= MAX_LINES);
    return &result_lines[result_lines_count++];
}

void parse_annotation_file() {
    char* const file_contents = read_file(annotate_filename, NULL);
    char* lines = file_contents;

    int lc = 0;

    // Determine lines to annotate.
    while (true) {
        char* line_end = NULL;
        char* line = next_line(&lines, &line_end);
        char* line_full_end = lines;  // Line with linebreak characters.

        if (!line)  break;  // No more lines.
        ++lc;

        char* prev_line_chars = line;
        char* curr_line_chars = line;

        char* annotation_word = NULL;
        char* annotation_word_end = NULL;
        bool  invalid_line = false;

        while (curr_line_chars != line_end) {
            prev_line_chars = curr_line_chars; 

            int codepoint = read_utf8_codepoint(&curr_line_chars, line_end - curr_line_chars);
            if (codepoint == UNICODE_EOF) {
                break;
            } else if (codepoint == UNICODE_INVALID_CHARACTER) {
                // Invalid UTF-8 character sequence.
                invalid_line = true;
                break;
            } else if (is_cjk_codepoint(codepoint) || is_kana_codepoint(codepoint)) {
                if (!annotation_word) {
                    annotation_word = prev_line_chars;
                }
                continue;
            } else {
                if (annotation_word)
                    break;
                else if (is_space_codepoint(codepoint))
                    continue;
            }
        }

        verify(!invalid_line);

        if (drop_lines_without_word && annotation_word == NULL)
            continue;

        auto result_line = new_result_line();
        result_line->line = line;
        result_line->line_end = line_full_end;

        if (annotation_word && !invalid_line) {
            annotation_word_end = prev_line_chars;

            result_line->word = annotation_word;
            result_line->word_end = annotation_word_end;
        }
    };
}

char collection_model_id[64];
int collection_model_primary_field_index = -1;
int collection_model_annotation_field_index = -1;

void collection_load_model(sqlite3* db) {
    char* query = "SELECT models FROM col";

    sqlite3_stmt* stmt = NULL;
    verify(SQLITE_OK == sqlite3_prepare_v2(db, query, strlen(query)+1, &stmt, 0));

    int status = sqlite3_step(stmt);
    verify(status != SQLITE_DONE);  // Row not found.
    verify(status == SQLITE_ROW);   

    char* json_text = (char*)sqlite3_column_text(stmt, 0);
    verify(json_text);

    json_text = _strdup(json_text);
    verify(json_text);

    sqlite3_finalize(stmt);
    stmt = NULL;
    
    json_state state;
    verify(json_parse(&state, json_text, strlen(json_text)));

    json_object* Model = NULL;

    auto root = state.root;
    for (auto ModelMember = root->first; ModelMember; ModelMember = ModelMember->next) {
        auto XModel = ModelMember->value->as_object();
        verify(XModel);

        auto model_name = XModel->get_string("name");
        if (!model_name)  continue;
        if (model_name->count != strlen(collection_model_name))  continue;
        if (0 != _strnicmp(model_name->chars, collection_model_name, strlen(collection_model_name)))  continue;

        verify(0 == strncpy_s(collection_model_id, ModelMember->name, ModelMember->name_count));

        Model = XModel;
        break;
    }

    verify(Model);  // Model missing.

    auto Fields = Model->get_array("flds");
    verify(Fields);

    for (auto FieldMember = Fields->first; FieldMember; FieldMember = FieldMember->next) {
        auto XField = FieldMember->value->as_object();
        verify(XField);

        auto field_name = XField->get_string("name");
        if (!field_name)  continue;

        int* field_index = NULL;

        // @TODO: Use UTF-8 string comparison (sqlite3_stricmp)

        if (field_name->count == strlen(collection_primary_field_name) &&
            0 == _strnicmp(field_name->chars, collection_primary_field_name, strlen(collection_primary_field_name)))
        {
            field_index = &collection_model_primary_field_index;
        }
        else if (field_name->count == strlen(collection_annotation_field_name) &&
                 0 == _strnicmp(field_name->chars, collection_annotation_field_name, strlen(collection_annotation_field_name)))
        {
            field_index = &collection_model_annotation_field_index;
        } 
        else {
            continue;
        }

        auto Ord = XField->get_number("ord");
        verify(Ord);

        *field_index = (int)Ord->number;
    }

    verify(collection_model_primary_field_index != -1);
    verify(collection_model_annotation_field_index != -1);

    json_free(&state);
}

char character_buffer[MAX_CHARACTER_BUFFER_SIZE];
int character_buffer_count = 0;

char* new_character_buffer_entry(int count) {
    verify(count >= 0);
    char* result = &character_buffer[character_buffer_count];
    character_buffer_count += count + 1;  // @TODO: Align by pointer?
    verify(character_buffer_count <= MAX_CHARACTER_BUFFER_SIZE);
    return result;
}

struct Note {
    char* primary = NULL;      // All strings come from character_buffer, don't deallocate.
    char* primary_end = NULL;
    char* annotate = NULL;
    char* annotate_end = NULL;
};
Note notes[MAX_NOTES];
int notes_count = 0;

Note* new_note() {
    verify(notes_count <= MAX_NOTES);
    return &notes[notes_count++];
}

// 'indices' must be sorted in ascending order.
void find_seperated_strings(char separator, const char* source, int count, int* indices, char** starts, char** ends) {
    assert(source);
    assert(count > 0);
    assert(indices);
    assert(starts);
    assert(ends);

    int current_separator = -1;
    const char* previous_separator_position = source;
    int current_index = *indices++;
    
    char c;
    do {
        c = *source;
        if (c == separator || c == '\0') {
            ++current_separator;

            if (current_separator == current_index) {
                *starts++ = (char*)previous_separator_position;
                *ends++   = (char*)source - 1;
                --count;
                if (count == 0)  break;
                current_index = *indices++;
            }

            previous_separator_position = source + 1;
            if (c == '\0')  break;
        }
        ++source;
    } while (true);

    // @TODO: Probably doesn't work with last separator (or maybe it does since it treats \0 as separator). 

    assert(count == 0);
}

int __cdecl compare_notes(void const* aa, void const* bb) {
    Note* a = (Note*)aa;
    Note* b = (Note*)bb;

    return sqlite3_strnicmp(a->primary, b->primary, max(a->primary_end - a->primary, b->primary_end - b->primary));
}

void build_note_cache(sqlite3* db) {
    char query[64] { 0 };
    StringCchPrintfA(query, ARRAYSIZE(query), "SELECT flds FROM notes WHERE mid = %s", collection_model_id);

    sqlite3_stmt* stmt = NULL;
    verify(SQLITE_OK == sqlite3_prepare_v2(db, query, strlen(query)+1, &stmt, NULL));

    while (true) {
        int status = sqlite3_step(stmt);
        if (status == SQLITE_DONE)  break;
        verify(status == SQLITE_ROW);

        const char* fields = (const char*)sqlite3_column_text(stmt, 0);
        verify(fields);

        const int total_fields = 2;

        int   field_indices[total_fields];
        if (collection_model_primary_field_index < collection_model_annotation_field_index) {
            field_indices[0] = collection_model_primary_field_index;
            field_indices[1] = collection_model_annotation_field_index;
        } else {
            field_indices[0] = collection_model_annotation_field_index;
            field_indices[1] = collection_model_primary_field_index;
        }
        char* field_starts[total_fields];
        char* field_ends[total_fields];

        find_seperated_strings(0x1f, fields, total_fields, field_indices, field_starts, field_ends);

        auto note = new_note();
        {
            int primary_size = field_ends[0] - field_starts[0] + 1;
            note->primary = strncpy(new_character_buffer_entry(primary_size), field_starts[0], primary_size);
            note->primary_end = note->primary + primary_size;
        }
        {
            int annotate_size = field_ends[1] - field_starts[1] + 1;
            note->annotate = strncpy(new_character_buffer_entry(annotate_size), field_starts[1], annotate_size);
            note->annotate_end = note->annotate + annotate_size;
        }
    }

    verify(SQLITE_OK == sqlite3_finalize(stmt));
    stmt = NULL;

    qsort(notes, notes_count, sizeof(Note), compare_notes);
}

Note* find_note(const char* primary, const char* primary_end) {
    Note placeholder;
    placeholder.primary = (char*)primary;
    placeholder.primary_end = (char*)primary_end;

    return (Note*)bsearch(&placeholder, notes, notes_count, sizeof(Note), compare_notes);
}

// All lines with annotations.
void write_annotations_v1(HANDLE annotated_file) {
    int can_apply = 0;
    for (int i = 0; i < result_lines_count; ++i) {
        auto& result_line = result_lines[i];
        if (result_line.word != NULL) {
            Note* note = find_note(result_line.word, result_line.word_end);
            if (note) {
                can_apply++;

                write_to_file(annotated_file, note->annotate, note->annotate_end - note->annotate);
                write_to_file(annotated_file, result_line.line, result_line.line_end - result_line.line);
                continue;
            }
        }

        // Line doesn't contain a word to annotate or word wasn't found in database.
        // @TODO: Batch lines without words and write them as one chunk.
        write_to_file(annotated_file, result_line.line, result_line.line_end - result_line.line);
    }
}

// Only lines with annonations.
void write_annotations_v2(HANDLE annotated_file) {
    int can_apply = 0;
    for (int i = 0; i < result_lines_count; ++i) {
        auto& result_line = result_lines[i];
        if (result_line.word != NULL) {
            Note* note = find_note(result_line.word, result_line.word_end);
            if (note) {
                can_apply++;

                write_to_file(annotated_file, note->annotate, note->annotate_end - note->annotate);
                write_to_file(annotated_file, result_line.line, result_line.line_end - result_line.line);
                continue;
            }
        }
    }
}

// Only lines with annonations + trim spaces
void write_annotations_v3(HANDLE annotated_file) {
    int can_apply = 0;
    for (int i = 0; i < result_lines_count; ++i) {
        auto& result_line = result_lines[i];
        if (result_line.word != NULL) {
            Note* note = find_note(result_line.word, result_line.word_end);
            if (note) {
                can_apply++;

                write_to_file(annotated_file, note->annotate, note->annotate_end - note->annotate);

                char* trim_line = result_line.line;
                while (true) {
                    char* now = trim_line;
                    int codepoint = read_utf8_codepoint(&now, result_line.line_end - now);
                    assert(codepoint != UNICODE_INVALID_CHARACTER);
                    if (codepoint == 0)  break;
                    if (is_space_codepoint(codepoint)) {
                        trim_line = now;
                    } else {
                        break;
                    }
                }

                write_to_file(annotated_file, " ", 1);
                write_to_file(annotated_file, trim_line, result_line.line_end - trim_line);
                continue;
            }
        }
    }
}

int __cdecl compare_lines(void const* aa, void const* bb) {
    auto a = (ResultLine*)aa;
    auto b = (ResultLine*)bb;

    Note* note_a = find_note(a->word, a->word_end);
    Note* note_b = find_note(b->word, b->word_end);

    if (note_a && note_b == NULL)  return -1;
    if (note_a == NULL && note_b)  return  1;

    const char prefix[] = "[sound:core/";
    int prefix_length = ARRAYSIZE(prefix) - 1;

    if (note_a && note_b) {
        int note_a_annotate_length = note_a->annotate_end - note_a->annotate;
        int note_b_annotate_length = note_b->annotate_end - note_b->annotate;

        if (note_a_annotate_length >= prefix_length && 
            note_b_annotate_length >= prefix_length &&
            sqlite3_strnicmp(note_a->annotate, prefix, prefix_length) == 0 && 
            sqlite3_strnicmp(note_b->annotate, prefix, prefix_length) == 0)
        {
            int id_a = atoi(&note_a->annotate[prefix_length]);
            int id_b = atoi(&note_b->annotate[prefix_length]);

            if (id_a != 0 && id_b != 0) {
                return id_a - id_b; // Ascending order.
            }
        }
        return sqlite3_strnicmp(note_a->annotate, note_b->annotate, max(note_a_annotate_length, note_b_annotate_length));
    }
    return sqlite3_strnicmp(a->word, b->word, max(a->word_end - a->word, b->word_end - b->word));
}

void sort_lines() {
    qsort(result_lines, result_lines_count, sizeof(ResultLine), compare_lines);
}

void write_annotations() {
    {
        sqlite3* anki = NULL;
        verify(SQLITE_OK == sqlite3_open(collection_filename, &anki));

        collection_load_model(anki);
        build_note_cache(anki);

        verify(SQLITE_OK == sqlite3_close(anki));
    }

    HANDLE annotated_file = create_file(annotate_result_filename);

    if (sort_output_lines)  sort_lines();
    write_annotations_impl(annotated_file);

    close_file(annotated_file);
    annotated_file = INVALID_HANDLE_VALUE;
}

int main(int argc, char** argv) {
    LARGE_INTEGER clock_frequency, tick_start, tick_end;
    QueryPerformanceFrequency(&clock_frequency);
    QueryPerformanceCounter(&tick_start);

    parse_annotation_file();
    write_annotations();

    QueryPerformanceCounter(&tick_end);

    char message[256] = { 0 };
    StringCchPrintfA(
        message, 
        ARRAYSIZE(message), 
        
        "Processing time: %lf seconds\n"
        "Character buffer usage: %u/%u\n"
        "Notes loaded: %u/%u\n"
        "Lines loaded: %u/%u\n\n",

        (tick_end.QuadPart - tick_start.QuadPart) / (double)clock_frequency.QuadPart,

        character_buffer_count,
        MAX_CHARACTER_BUFFER_SIZE,

        notes_count,
        MAX_NOTES,

        result_lines_count,
        MAX_LINES);
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), message, strlen(message), NULL, NULL);

    return 0;
}
