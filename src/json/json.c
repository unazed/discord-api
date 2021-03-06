#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include "json.h"
#include "grammar.h"

size_t
json_parse_array (json_array_t** out, const char *string)
{
  json_array_t *array = (json_array_t *)calloc (DEFAULT_BINSIZE, sizeof (json_array_t));
  json_array_t current_token = {0};

  parse_state_t pstate = {
    .expecting_key = false,       /* unused */
    .expecting_value_sep = false,
    .expecting_name_sep = false,  /* unused */
    .object_literals = 0,         /* unused */
    .array_literals = 1
    };

  size_t idx = 0,
         item_count = 0,
         idx_skip;

  for (;;)
    {
      switch (string[idx])
        {
          case (QUOTE_MARK):
          {
            if (pstate.expecting_value_sep)
              {
                json_print_error ("parse", "expecting value separator, got string");
                goto on_error;
              }
            idx += json_parse_string (&current_token.as_string, &string[idx+1]) + 1;
            current_token.__dynamic_flag = DYN_STRING_MASK;
            pstate.expecting_value_sep = true;
            break;
          }
          case (BEGIN_OBJECT):
          {
            if (pstate.expecting_value_sep)
              {
                json_print_error ("parse", "expecting value separator, got object");
                goto on_error;
              }
            current_token.as_object = json_parse_item (&string[idx+1], &idx_skip);
            current_token.__dynamic_flag = DYN_ITEM_MASK;
            idx += idx_skip + 1;
            pstate.expecting_value_sep = true;
            break;
          }
          case (VALUE_SEP):
          {
            if (!pstate.expecting_value_sep)
              {
                json_print_error ("parse", "got value separator when one was not expected");
                goto on_error;
              }
            memcpy (&array[item_count++], &current_token, sizeof (json_array_t));
            memset (&current_token, 0, sizeof (json_array_t));
            pstate.expecting_value_sep = false;
            idx++;
            break;
          }
          case (END_ARRAY):
          {
            memcpy (&array[item_count++], &current_token, sizeof (json_array_t));
            memset (&current_token, 0, sizeof (json_array_t));
            idx++;
            goto realloc_array;
          }
          case (BEGIN_ARRAY):
          {
            if (pstate.expecting_value_sep)
              {
                json_print_error ("parse", "expecting value separator, got array");
                goto on_error;
              }
            idx += json_parse_array (&current_token.as_array, &string[idx+1]) + 1;
            current_token.__dynamic_flag = DYN_ARRAY_MASK;
            pstate.expecting_value_sep = true;
            break;
          }
          default:
          {
            if (isspace (string[idx]))  /* RFC compliant */
              {
                idx++;
                continue;
              }
            else if (pstate.expecting_key)
              {
                json_print_error ("parse", "non-string keys disallowed");
                goto on_error;
              }
            else if (!strncmp (&string[idx], LITERAL_FALSE, strlen (LITERAL_FALSE)))
              {
                current_token.as_boolean = false;
                idx += strlen (LITERAL_FALSE);
              }
            else if (!strncmp (&string[idx], LITERAL_TRUE, strlen (LITERAL_TRUE)))
              {
                current_token.as_boolean = true;
                idx += strlen (LITERAL_TRUE);
              }
            else if (!strncmp (&string[idx], LITERAL_NULL, strlen (LITERAL_TRUE)))
              {
                current_token.as_null = NULL;
                idx += strlen (LITERAL_NULL);
              }
            else
              {
                json_print_error ("parse", "invalid literal specifier in value");
                printf ("invalid specifier is '%c'\n", string[idx]);
                goto on_error;
              }
            pstate.expecting_value_sep = true;
          }
        }
    }
realloc_array:
  realloc (array, sizeof (json_array_t) * item_count + 2);  /* extra null structure for freeing later */
  *out = array;
  return idx;
on_error:
  *out = NULL;
  return 0;
}

void
json_print_error (const char *nature, const char *string)
{
  printf ("%s error: %s\n", nature, string);
}

size_t
json_parse_string (json_string_t* out, const char *string)
{
  json_string_t parsed = (json_string_t)calloc (DEFAULT_STRINGSIZE, sizeof (char));

  bool escape_seq = false;

  for (size_t idx = 0;; ++idx)
    {
      switch (string[idx])
        {
          case (ESCAPE):
          {
            if (escape_seq)
                strcat ((char *)parsed, "\\");
            else
              escape_seq = true;
            break;
          }
          case (QUOTE_MARK):
          {
            if (escape_seq)
              {
                strcat ((char *)parsed, "\"");
                escape_seq = false;
              }
            else
              {
                *out = parsed;
                return idx + 1;
              }
            break;
          }
          default:
          {
            strncat ((char *)parsed, &string[idx], 1);
          }
        }
    }
}

bool
json_parse_numeral (double *result, const char *string, size_t *length)
{
  *result = 0;
  ssize_t idx = 0, length_to_dp = 0;
  bool is_double = false;
  for (; isdigit (string[length_to_dp]); ++length_to_dp);
  for (ssize_t f_idx = 0; isdigit (string[idx]) || string[idx] == '.'; ++idx)
    {
      if (string[idx] == '.')
        {
          is_double = true;
          continue;
        }
      *result += (double) (string[idx] - '0') * pow (10, length_to_dp - f_idx - 1);
      f_idx++;
    }
  *length = idx;
  return is_double;
}

json_item_t*
json_parse_item (const char *string, size_t *idx_skip)
{
  json_item_t *items = (json_item_t *)calloc (DEFAULT_BINSIZE, sizeof (json_item_t));
  json_item_t current_token;
  current_token.__dynamic_flag = 0;
  parse_state_t pstate = {
    .expecting_key = true,
    .expecting_value_sep = false,
    .expecting_name_sep = false,
    .object_literals = 1,
    .array_literals = 0
    };

  size_t item_count = 0;
  size_t idx = 0, item_idx;
  for (;;)
    {
      switch (string[idx])
        {
          case (BEGIN_ARRAY):
          {
            if (pstate.expecting_key)
              {
                json_print_error ("parse", "key cannot be array");
                goto on_error;
              }
            idx += json_parse_array (&current_token.value.as_array, &string[idx+1]) + 1;
            current_token.__dynamic_flag = DYN_ARRAY_MASK;
            if (current_token.value.as_array == NULL)
                goto on_error;
            pstate.expecting_value_sep = true;
            pstate.expecting_key = false;
            break;
          }
          case (QUOTE_MARK):
          {
            if (pstate.expecting_key)
              {
                idx += json_parse_string (&current_token.key, &string[idx+1]) + 1;
                current_token.__dynamic_flag = DYN_STRING_MASK;
                pstate.expecting_name_sep = true;
                pstate.expecting_key = false;
              }
            else
              {
                if (current_token.key == NULL)
                  {
                    json_print_error ("parse", "expecting value, but no key parsed");
                    goto on_error;
                  }
                  idx += json_parse_string (&current_token.value.as_string, &string[idx+1]) + 1;
                  current_token.__dynamic_flag = DYN_STRING_MASK;
                  pstate.expecting_value_sep = true;
              }
            break;
          }
          case (NAME_SEP):
          {
            if (pstate.expecting_key)
              {
                json_print_error ("parse", "name separator encountered when expecting key");
                goto on_error;
              }
            else if (!pstate.expecting_name_sep)
              {
                json_print_error ("parse", "found name separator when not expecting one");
                goto on_error;
              }
            pstate.expecting_name_sep = false;
            idx++;
            break;
          }
          case (VALUE_SEP):
          {
            if (!pstate.expecting_value_sep)
              {
                json_print_error ("parse", "expecting value separator");
                goto on_error;
              }
            memcpy (&items[item_count++], (void *)&current_token, sizeof (json_item_t));
            memset (&current_token, 0, sizeof (json_item_t));
            if (item_count > DEFAULT_BINSIZE)
              {
                json_print_error ("internal", "exceeded default binsize");
                goto on_error;
              }
            pstate.expecting_key = true;
            pstate.expecting_value_sep = false;
            idx++;
            break;
          }
          case (END_OBJECT):
          {
            if (pstate.expecting_value_sep)
              {
                memcpy (&items[item_count++], (void *)&current_token, sizeof (json_item_t));
                memset (&current_token, 0, sizeof (json_item_t));
              }
            if (pstate.object_literals - 1 == 0)
                goto realloc_proc;
          }
          case (BEGIN_OBJECT):
          {
            if (pstate.expecting_key)
              {
                json_print_error ("parse", "object can't be key");
                goto on_error;
              }
            current_token.value.as_object = json_parse_item (&string[idx + 1], &item_idx);
            current_token.__dynamic_flag = DYN_ITEM_MASK;
            idx += item_idx + 1;
            pstate.expecting_value_sep = true;
            break;
          }
          default:
          {
            if (isspace (string[idx]))  /* RFC compliant */
              {
                idx++;
                continue;
              }
            else if (pstate.expecting_key)
              {
                json_print_error ("parse", "non-string keys disallowed");
                goto on_error;
              }
            else if (!strncmp (&string[idx], LITERAL_FALSE, strlen (LITERAL_FALSE)))
              {
                current_token.value.as_boolean = false;
                idx += strlen (LITERAL_FALSE);
              }
            else if (!strncmp (&string[idx], LITERAL_TRUE, strlen (LITERAL_TRUE)))
              {
                current_token.value.as_boolean = true;
                idx += strlen (LITERAL_TRUE);
              }
            else if (!strncmp (&string[idx], LITERAL_NULL, strlen (LITERAL_TRUE)))
              {
                current_token.value.as_null = NULL;
                idx += strlen (LITERAL_NULL);
              }
            else if (isdigit (string[idx]))
              {
                double result;
                size_t length;
                if (json_parse_numeral (&result, &string[idx], &length))
                  {
                    current_token.value.as_decimal = (json_double_t)result;
                    idx += length;
                  }
                else
                  {
                    current_token.value.as_integer = (json_int_t)result;
                    idx += length;
                  }
              }
            else
              {
                json_print_error ("parse", "invalid literal specifier in value");
                printf ("invalid specifier is '%c'\n", string[idx]);
                goto on_error;
              }
            current_token.__dynamic_flag = 0;
            pstate.expecting_value_sep = true;
            break;
          }
        }
    }
realloc_proc:
  realloc (items, sizeof (json_item_t) * item_count + 1);
  if (idx_skip != NULL)
    *idx_skip = idx + 1;
  return items;
on_error:
  return NULL;
}

json_generic_t
json_loadstring (const char *string)
{
  json_generic_t generic = {0};
  switch (string[0])
    {
      case (BEGIN_ARRAY):
        json_parse_array (&generic.as_array, &string[1]);
        break;
      case (BEGIN_OBJECT):
        generic.as_object = json_parse_item (&string[1], NULL);
        break;
      default:
        return generic;
    }
  return generic;
}

json_item_t*
json_get_nth (json_item_t* json, size_t n)
{
  return &(((json_item_t *)json)[n]);
}

void
json_free_array (json_array_t *array)
{
  json_array_t *current_array;
  for (size_t idx = 0; current_array = &array[idx]; ++idx)
    {
      if (!current_array->__dynamic_flag)
        continue;
      else if (current_array->__dynamic_flag > 0x4)  /* honestly don't even ask */
        break;

      switch (current_array->__dynamic_flag)
        {
          case (DYN_STRING_MASK):
          {
            free ((void *)current_array->as_string);
            break;
          }
          case (DYN_ITEM_MASK):
          {
            json_free_item (current_array->as_object);
            break;
          }
          case (DYN_ARRAY_MASK):
          {
            json_free_array (current_array->as_array);
            break;
          }
        }
    }
    free ((void *)array);
}

void
json_free_item (json_item_t *item)
{
  json_item_t *current_item;
  for (size_t idx = 0; current_item = json_get_nth (item, idx); ++idx)
    {
      if (!current_item->__dynamic_flag)
        continue;
      else if (current_item->__dynamic_flag > 0x4)
        break;

      switch (current_item->__dynamic_flag)
        {
          case (DYN_STRING_MASK):
          {
            free ((void *)current_item->value.as_string);
            break;
          }
          case (DYN_ITEM_MASK):
          {
            json_free_item (current_item->value.as_object);
            break;
          }
          case (DYN_ARRAY_MASK):
          {
            json_free_array (current_item->value.as_array);
            break;
          }
        }
    }
    free ((void *)item);
}

int
main (void)
{
  json_item_t *json = json_loadstring ("{\"abc\": 1}").as_object;
  if (json == NULL)
    {
      puts ("error occurred in parsing");
      return EXIT_FAILURE;
    }
  json_free_item (json);
  return EXIT_SUCCESS;
}
