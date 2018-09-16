#ifndef _JSON_H_
#define _JSON_H_

enum json_value_type
{
    json_type_false,
    json_type_null,
    json_type_true,
    json_type_object,   // json_object_t
    json_type_array,    // json_array_t
    json_type_number,   // json_number_t
    json_type_string    // json_string_t
};

struct json_object;
struct json_string;
struct json_array;
struct json_number;

struct json_value
{
    json_value_type type;

    inline json_object* as_object() const { return type == json_type_object ? (json_object*)this : 0; }
    inline json_string* as_string() const { return type == json_type_string ? (json_string*)this : 0; }
    inline json_number* as_number() const { return type == json_type_number ? (json_number*)this : 0; }
    inline json_array*  as_array()  const { return type == json_type_array  ? (json_array*) this : 0; }
};

struct json_object_member
{
    const char* name = 0;
    size_t name_count = 0;

    json_value* value = 0;
    json_object_member* next = 0;
};

struct json_object : json_value
{
    json_object_member* first = 0;
    size_t nmembers = 0;

    /**
    * Searches for object member with specified key name.
    * Returns null if no key with such name exists.
    */
    json_object_member* find_key(const char* name) const;

    /**
    * Searches for object member with specified key name and specified element value type.
    * Returns null if no key with such name exists of value type doesn't match.
    */
    json_object_member* find_key_with_value_type(const char* name, json_value_type value_type);

    json_string* get_string(const char* key_name);
    json_array* get_array(const char* key);
    json_number* get_number(const char* key);
};

struct json_array_member
{
    json_value* value = 0;
    json_array_member* next = 0;
};

struct json_array : json_value
{
    json_array_member* first = 0;
    size_t nmembers = 0;
};

struct json_number : json_value
{
    double number = 0;
};

struct json_string : json_value
{
    const char* chars = 0;
    size_t count = 0;

    bool equals(const char* string, size_t count, bool case_insensitive) const;
};

struct json_state
{
    const char* src = NULL;
    const char* now = NULL;
    const char* end = NULL;

    json_object* root = NULL;
    bool valid = false;
    const char* error_message = NULL;
};


bool json_parse(json_state* state, const char* src, size_t count);
void json_free(json_state* state);
void json_dump(json_state* state);


#ifdef JSON_IMPLEMENTATION

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


static const char* json_parse_string(json_state* state, size_t* out_count);
static json_value* json_parse_value(json_state* state);
static json_object* json_parse_object(json_state* state);


static const char* json_skip(json_state* state, const char* now = NULL) {
    assert(state);

    if (now)  state->now = now;
    now = state->now;

    if (now >= state->end)
        return 0;

    for (; now < state->end; ++now)
    {
        char c = *now;
        if (c == ' ' || c == '\x09' || c == '\x0A' || c == '\x0D')
            continue;
        else
            break;
    }

    state->now = now;
    return now < state->end ? now : 0;
}

static json_number* json_parse_number(json_state* state) {
    assert(state);

    const char* now = json_skip(state);
    if (!now) {
        state->valid = false;
        state->error_message = "Expected number.";
        return 0;
    }

    int nreaded;
    double dval;
    if ((1 != _snscanf_s(now, state->end - now, "%lf%n", &dval, &nreaded)) || (nreaded <= 0)) {
        state->valid = false;
        state->error_message = "Invalid number.";
        return 0;
    }
    state->now = now + nreaded;

    json_number* number = new json_number();
    number->type = json_type_number;
    number->number = dval;
    return number;
}

static json_array* json_parse_array(json_state* state)
{
    assert(state);

    const char* now = json_skip(state);
    if (!now || now[0] != '[') {
        state->valid = false;
        state->error_message = "Expected array.";
        return 0;
    }
    now = state->now = now + 1;

    json_array_member* first = 0;
    json_array_member* last = 0;
    size_t nitems = 0;
    while (1)
    {
        // Handle empty array case.
        if (nitems == 0)
        {
            now = json_skip(state);
            if (now && now[0] == ']')
                goto array_end;
        }

        json_value* value = json_parse_value(state);
        if (!value)
            return 0; // @Leak: first, last

        json_array_member* item = new json_array_member();
        item->value = value;
        item->next = 0;
        ++nitems;

        if (!first) first = item;
        if (!last)  last  = item;
        else
        {
            last->next = item;
            last = item;
        }

        now = json_skip(state);
        if (!now) {
            state->valid = false;
            state->error_message = "Expected array member.";
            return 0;
        }

        if (now[0] == ',')
            now = state->now = now + 1;
        else if (now[0] == ']')
            goto array_end;
        else
            break;
    }

    state->valid = false;
    state->error_message = "Invalid array.";

    return NULL;

array_end:
    json_array* array = new json_array();
    array->type = json_type_array;
    array->nmembers = nitems;
    array->first = first;

    state->now = now + 1; // Don't forget closing ']' bracket.
    return array;
}

static json_value* json_parse_value(json_state* state)
{
    assert(state);

    const char* now = json_skip(state);
    if (!now) {
        state->valid = false;
        state->error_message = "Expected value.";
        return 0;
    }

    if (now[0] == '"')
    {
        size_t count;
        const char* chars = json_parse_string(state, &count);
        if (!chars) {
            state->valid = false;
            state->error_message = "Expected string.";
            return 0;
        }

        json_string* s = new json_string();
        s->type = json_type_string;
        s->chars = chars;
        s->count = count;

        return s;
    }
    else if (now[0] == '{')
    {
        return json_parse_object(state);
    }
    else if (now[0] == '[')
    {
        return json_parse_array(state);
    }
    else if ((now[0] >= '0' && now[0] <= '9') || now[0] == '-' || now[0] == '+')
    {
        return json_parse_number(state);
    }
    else if (now + 4 < state->end && now[0] == 'n' && now[1] == 'u' && now[2] == 'l' && now[3] == 'l')
    {
        json_value* value = new json_value();
        value->type = json_type_null;
        state->now += 4;
        return value;
    }
    else if (now + 4 < state->end && now[0] == 't' && now[1] == 'r' && now[2] == 'u' && now[3] == 'e')
    {
        json_value* value = new json_value();
        value->type = json_type_true;
        state->now += 4;
        return value;
    }
    else if (now + 5 < state->end && now[0] == 'f' && now[1] == 'a' && now[2] == 'l' && now[3] == 's' && now[4] == 'e')
    {
        json_value* value = new json_value();
        value->type = json_type_false;
        state->now += 5;
        return value;
    }
    else
    {
        state->valid = false;
        state->error_message = "Invalid value.";
        return 0;
    }
}

// Private
static json_object* json_parse_object(json_state* state)
{
    assert(state);

    const char* now = json_skip(state);
    if (!now)
    {
        state->valid = false;
        state->error_message = "Expected object.";
        return 0;
    }

    if (now[0] != '{')
    {
        state->valid = false;
        state->error_message = "Expected object.";
        return 0;
    }
    now = state->now = now + 1;

    json_object_member* first = 0;
    json_object_member* last  = 0;
    size_t nitems = 0;
    while (1)
    {
        // Handle empty object case.
        if (nitems == 0)
        {
            now = json_skip(state);
            if (now && now[0] == '}')
                goto object_end;
        }

        size_t key_count;
        auto key = json_parse_string(state, &key_count);
        if (!key)
            goto error;

        now = json_skip(state);
        if (!now || now[0] != ':')
            goto error;
        now = state->now = now + 1;

        json_value* value = json_parse_value(state);
        if (!value)
            goto error;

        json_object_member* item = new json_object_member();
        item->name = key;
        item->name_count = key_count;
        item->value = value;
        item->next = 0;
        ++nitems;

        if (!first) first = item;
        if (!last)  last  = item;
        else 
        {
            last->next = item;
            last = item;
        }

        // Handle comma and closing bracket.
        now = json_skip(state);
        if (!now)
            goto error;

        if (now[0] == ',')
        {
            now = state->now = now + 1;
            continue;
        }
        else if (now[0] == '}')
            goto object_end;
        else
            goto error;
    }

error:
    if (first)  // @TODO: Check same thing for parse_array
    {
        json_object_member* next = first->next;
        delete first;
        first = next;
    }

    state->valid = false;
    state->error_message = "Invalid object.";
    return 0;

object_end:
    json_object* o = new json_object();
    o->type = json_type_object;
    o->first = first;
    o->nmembers = nitems;

    state->now = now + 1; // Don't forget closing '}' bracket.
    return o;
}

static const char* json_parse_string(json_state* state, size_t* out_count)
{
    assert(state);
    assert(out_count);

    const char* now = json_skip(state);
    if (now >= state->end)
    {
        state->valid = false;
        state->error_message = "Expected string.";
        return 0;
    }

    if (now[0] != '\"')
    {
        state->valid = false;
        state->error_message = "Invalid string.";
        return 0;
    }

    const char* chars = ++now;
    size_t count = 0;

    while (now < state->end)
    {
        // @TODO: handle escape sequences.
        char c = *now++;
        if (c == '\\')
        {
            if (now >= state->end)
            {
                state->valid = false;
                state->error_message = "Invalid escape sequence.";
                return 0;
            }
            char n = *now++;
            switch (n)
            {
                case '"':
                case '\\':
                case '/':
                case 'b':
                case 'f':
                case 'r':
                case 't':
                case 'n':
                    continue;
                case 'u':
                {
                    // https://tools.ietf.org/html/rfc7159#section-2
                    // @TODO: Handle UTF16
                    continue;
                }
            }

            // Invalid escape sequence.
            state->valid = false;
            state->error_message = "Invalid escape sequence.";
            return 0;
        }

        if (c == '\"')
        {
            count = now - chars - 1;
            goto ok;
        }
    }

    state->valid = false;
    state->error_message = "Invalid string.";
    return 0;

ok:
    state->now = now;
    *out_count = count;
    return chars;
}


bool json_parse(json_state* state, const char* src, size_t count)
{
    assert(state);
    assert(src);

    state->src = src;
    state->now = src;
    state->end = src + count;
    state->valid = true;
    state->root = json_parse_object(state);

    return state->valid;
}

static void json_free_value(json_value* value)
{
    switch (value->type)
    {
        case json_type_true:
        case json_type_false:
        case json_type_null:
        case json_type_number:
        case json_type_string:
            break;
        case json_type_object:
        {
            auto object = (json_object*)value;
            auto now = object->first;
            while (now)
            {
                if (now->value)
                    json_free_value(now->value);
                auto next = now->next;
                delete now;
                now = next;
            }
            break;
        }
        case json_type_array:
        {
            auto array = (json_array*)value;
            auto now = array->first;
            while (now)
            {
                if (now->value)
                    json_free_value(now->value);
                auto next = now->next;
                delete now;
                now = next;
            }
            break;
        }
        default:
            return;
    }

    delete value;
}

void json_free(json_state* state)
{
    assert(state);

    state->valid = false;
    if (state && state->root)
        json_free_value(state->root);
    state->root = NULL;
}

void json_print_value(json_value* value)
{
    switch (value->type)
    {
        case json_type_null:
        {
            printf("null");
            break;
        }
        case json_type_true:
        {
            printf("true");
            break;
        }
        case json_type_false:
        {
            printf("false");
            break;
        }
        case json_type_number:
        {
            printf("%f", ((json_number*)value)->number);
            break;
        }
        case json_type_string:
        {
            auto string = ((json_string*)value);
            printf("\"%.*s\"", string->count, string->chars);
            break;
        }
        case json_type_array:
        {
            auto array = ((json_array*)value);
            auto item = array->first;
            if (array->nmembers == 0)
            {
                printf("[]");
                break;
            }
            printf("[ ");
            for (size_t i = 0; i < array->nmembers; ++i, item = item->next)
            {
                json_print_value(item->value);
                if (i != array->nmembers - 1)
                    printf(", ");
            }
            printf(" ]");
            break;
        }
        case json_type_object:
        {
            auto object = ((json_object*)value);
            auto item = object->first;
            if (object->nmembers == 0)
            {
                printf("{}");
                break;
            }
            printf("{ ");
            for (size_t i = 0; i < object->nmembers; ++i, item = item->next)
            {
                printf("\"%.*s\": ", item->name_count, item->name);

                json_print_value(item->value);
                if (i != object->nmembers - 1)
                    printf(", ");
            }
            printf(" }");
            break;
        }
        default:
        {
            assert(false);
        }
    }
}

void json_dump(json_state* state)
{
    assert(state);
    assert(state->root);

    json_print_value(state->root);
}

json_object_member* json_object::find_key(const char* name) const
{
    if (!name) return 0;

    size_t name_count = strlen(name);

    json_object_member* member = this->first;
    while (member)
    {
        if (member->name_count == name_count && _strnicmp(member->name, name, name_count) == 0)
            break;
        member = member->next;
    }
    return member;
}

json_object_member* json_object::find_key_with_value_type(const char* name, json_value_type value_type)
{
    json_object_member* member = find_key(name);
    return member->value->type == value_type ? member : 0;
}

json_string* json_object::get_string(const char* key_name)
{
    if (!key_name) return 0;

    json_object_member* member = find_key_with_value_type(key_name, json_type_string);
    return member ? member->value->as_string() : 0;
}

json_array* json_object::get_array(const char* key) {
    assert(key);
    json_object_member* member = find_key_with_value_type(key, json_type_array);
    return member ? member->value->as_array() : NULL;
}

json_number* json_object::get_number(const char* key){
    assert(key);
    json_object_member* member = find_key_with_value_type(key, json_type_number);
    return member ? member->value->as_number() : NULL;
}

bool json_string::equals(const char* string, size_t count, bool case_insensitive) const
{
    if (!string) return false;
    if (count != this->count) return false;

    return case_insensitive ? _strnicmp(chars, string, count) == 0 : strncmp(chars, string, count) == 0;
}

#endif
#endif