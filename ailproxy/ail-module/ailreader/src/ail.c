/*
  Copyright 2011 Casper Svenning Jensen. All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, are
  permitted provided that the following conditions are met:

     1. Redistributions of source code must retain the above copyright notice, this list of
        conditions and the following disclaimer.

     2. Redistributions in binary form must reproduce the above copyright notice, this list
        of conditions and the following disclaimer in the documentation and/or other materials
        provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY CASPER SVENNING JENSEN ``AS IS'' AND ANY EXPRESS OR IMPLIED
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL CASPER SVENNING JENSEN
  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  The views and conclusions contained in the software and documentation are those of the
  authors and should not be interpreted as representing official policies, either expressed
  or implied, of Casper Svenning Jensen
*/

#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <yajl/yajl_tree.h>

#include "ail.h"
#include "lemon.h"

int construct_ail(ail_t * ail, const char * raw_ail) {

  *ail = (ail_t) malloc(sizeof(struct ail));

  // set default values
  (*ail)->domain = "";
  (*ail)->operations = NULL;
  (*ail)->num_operations = 0;

  return parse_ail(*ail, raw_ail);

}

int allocate_operation(ail_operation_t * operation) {

  *operation = (ail_operation_t) malloc(sizeof(struct ail_operation));

  (*operation)->next = NULL;
  (*operation)->kwargs = NULL;
  (*operation)->fragments = NULL;
  (*operation)->schemas = NULL;

  return 0;
}

int allocate_schema(ail_schema_t * schema) {
  
  *schema = (ail_schema_t) malloc(sizeof(struct ail_schema));

  (*schema)->next = NULL;
  (*schema)->file_name = NULL;
  (*schema)->payload = NULL;

  return 0;
}

int allocate_url_fragment(ail_url_fragment_t * fragment) {
  
  *fragment = (ail_url_fragment_t) malloc(sizeof(struct ail_url_fragment));

  (*fragment)->next = NULL;
  (*fragment)->text = NULL;

  return 0;
}

int allocate_url_kwarg(ail_url_kwarg_t * kwarg) {
  
  *kwarg = (ail_url_kwarg_t) malloc(sizeof(struct ail_url_kwarg));

  (*kwarg)->next = NULL;
  (*kwarg)->keyword = NULL;
  (*kwarg)->value = NULL; 

  return 0;
}

int get_operation_for_request(const ail_t ail, ail_operation_t * operation, \
                              char ** args, int argc, char** kwargs, char** vwargs, int kwargc) {

  ail_operation_t current = ail->operations;

  int i;
  for (i = 0; i < ail->num_operations; i++) {

    if (operationcmp(current, args, argc, kwargs, vwargs, kwargc) == 0) {
      *operation = current;
      return 0; 
    }

    current = current->next;
  }

  return 1;
}

ail_response_t _last(ail_response_t start) {
  ail_response_t last = start;
  while (last->next != NULL) {
    last = last->next; 
  }

  return last;
}

int _recursive_operation_perm(const yajl_val schema_part, ail_response_t * response) {
  
  if (!YAJL_IS_OBJECT(schema_part)) {
    fprintf(stderr, "Error in schema format...");
    return 1;
  }

  yajl_val type = yajl_tree_get(schema_part, \
                                (const char *[]){"type", (const char*) 0},\
                                yajl_t_string);

  if (type == NULL) {
    fprintf(stderr, "Error getting type"); 
    return 1;
  }

  /* HANDLE OBJECT */
  /* TODO: Handle optional */

  if (YAJL_IS_STRING(type) && strcmp(type->u.string, "object") == 0) {

    yajl_val properties = yajl_tree_get(schema_part,\
                                        (const char*[]){"properties", (const char*) 0},\
                                        yajl_t_object);

    struct response_chunk * prepend = malloc(sizeof(struct response_chunk));
    prepend->chunk = "{";
    prepend->next = NULL;

    struct response_chunk * previous = NULL;

    if (properties == NULL || !YAJL_IS_OBJECT(properties)) {
      fprintf(stderr, "Error parsing properties");
      return 1;
    }

    int i;
    for (i = 0; i < properties->u.object.len; i++) {

      struct response_chunk * key = malloc(sizeof(struct response_chunk));
      key->chunk = malloc(sizeof(char) * (strlen(properties->u.object.keys[i]) + 4));
      key->next = NULL;

      sprintf(key->chunk, "\"%s\": ", properties->u.object.keys[i]);

      struct response_chunk * value;
      if (_recursive_operation_perm(properties->u.object.values[i], &value) != 0) {
        return 1;
      }

      key->next = value;

      if (previous == NULL) {
        prepend->next = key;
        previous = _last(value);
      } else {
        struct response_chunk * seperator = malloc(sizeof(struct response_chunk));
        seperator->chunk = ", ";
        seperator->next = key;

        previous->next = seperator;
        previous = _last(value);
      }

    }

    struct response_chunk * append = malloc(sizeof(struct response_chunk));
    append->chunk = "}";
    append->next = NULL;

    previous->next = append;

    *response = prepend;

    return 0;

  /* HANDLE ARRAY */

  } else if (YAJL_IS_STRING(type) && strcmp(type->u.string, "array") == 0) {

    yajl_val items = yajl_tree_get(schema_part,\
                                    (const char*[]){"items", (const char*) 0},\
                                    yajl_t_array);

    if (items == NULL || !YAJL_IS_ARRAY(items)) {
      fprintf(stderr, "Could not find states info");
      return 1;
    }

    struct response_chunk * prepend = malloc(sizeof(struct response_chunk));
    prepend->chunk = "[";
    prepend->next = NULL;

    ail_response_t last;
    last = NULL;

    int num_elements = random() % 20;

    int i;
    for (i = 0; i < num_elements; i++) {
      
      int choice = random() % items->u.array.len;

      ail_response_t element;
      _recursive_operation_perm(items->u.array.values[choice], &element);

      if (last == NULL) {
        
        prepend->next = element;
        last = _last(element);

      } else {

        struct response_chunk * seperator = malloc(sizeof(struct response_chunk));
        seperator->chunk = ", ";
        seperator->next = element;

        last->next = seperator;
        last = _last(element); 
         
      }

    }

    struct response_chunk * append = malloc(sizeof(struct response_chunk));
    append->chunk = "]";
    append->next = NULL;  

    if (last == NULL) {
      _last(prepend)->next = append;
    } else {
      _last(last)->next = append;
    }

    *response = prepend;
    return 0;

  /* HANDLE UNION */

  } else if (YAJL_IS_ARRAY(type)) {

    int selected_choice = random() % type->u.array.len;

    return _recursive_operation_perm(type->u.array.values[selected_choice], response);

  /* HANDLE BOOL */

  } else if (YAJL_IS_STRING(type) && strcmp(type->u.string, "boolean") == 0) {

    struct response_chunk * result = malloc(sizeof(struct response_chunk));
    result->next = NULL;

    int outcome = random() % 2;

    if (outcome) {
      result->chunk = "true"; 
    } else {
      result->chunk = "false"; 
    }

    *response = result;
    return 0;

  /* HANDLE STRING */
  /* TODO: IMPLEMENT */

  } else if (YAJL_IS_STRING(type) && strcmp(type->u.string, "string") == 0) {

    struct response_chunk * result = malloc(sizeof(struct response_chunk));
    result->chunk = "<str>";
    result->next = NULL;

    *response = result;
    return 0;      

  /* HANDLE INTEGER */

  } else if (YAJL_IS_STRING(type) && strcmp(type->u.string, "integer") == 0) {

    struct response_chunk * result = malloc(sizeof(struct response_chunk));
    result->chunk = malloc(sizeof(char) * 11);
    result->next = NULL;

    snprintf(result->chunk, 11, "%d", random());

    *response = result;
    return 0;    

  /* HANDLE NULL */

  } else if (YAJL_IS_STRING(type) && strcmp(type->u.string, "null") == 0) {

    struct response_chunk * result = malloc(sizeof(struct response_chunk));
    result->chunk = "null";
    result->next = NULL;

    *response = result;
    return 0;  

  /* HANDLE NON-JSON */

  } else if (YAJL_IS_STRING(type) && strcmp(type->u.string, "number") == 0) {

    struct response_chunk * result = malloc(sizeof(struct response_chunk));
    result->chunk = "<float>";
    result->next = NULL;

    *response = result;
    return 0;

  /* UNKNOWN TYPE */
  } else {
    fprintf(stderr, "Unknown type %s", type->u.string);
    return 1;
  }

  return 1;
}

int generate_response_permutation(const ail_operation_t operation, ail_response_t * response) {

  if (operation == NULL) {
    printf("Error: No operation");
    return 1;
  }

  if (operation->schemas == NULL) {
    printf("Error: No schema");
    return 1;
  }

  if (operation->schemas->payload == NULL) {
    printf("Error: No schema payload");
    return 1;
  }

  int count = 0;
  ail_schema_t current = operation->schemas;

  while (current != NULL) {
    count++;
    current = current->next;
  }

  int selection = random() % count;

  current = operation->schemas;
  int i;
  for (i = 0; i < selection; i++) {
    current = current->next;
  }

  return _recursive_operation_perm(current->payload, response);
}

int operationcmp(const ail_operation_t operation, \
                char** args, int argc, char** kwargs, char** vwargs, int kwargc) {

  ail_url_fragment_t current_fragment = operation->fragments;



  int i;
  for (i = 0; i < argc; i++) {

    if (current_fragment == NULL) { 
      return 1;
    }

    if (strcmp(current_fragment->text, args[i]) != 0) {
      return 1; 
    }

    current_fragment = current_fragment->next;
  }

  int count = 0;
  ail_url_kwarg_t current_kwarg = operation->kwargs;

  while (current_kwarg != NULL) {
    uint match = 0;

    for (i = 0; i < kwargc; i++) {
      
      if (strcmp(current_kwarg->keyword, kwargs[i]) == 0 && \
          (strcmp(current_kwarg->value, "*") == 0 || strcmp(current_kwarg->value, vwargs[i]) == 0)) {
            match = 1;
          }

    }

    if (match == 0) {
      return 1;
    }

    count++;
    current_kwarg = current_kwarg->next;
  }

  if (count != kwargc) {
    return 1;
  }

  return 0;
}

int load_schema(ail_schema_t schema) {
  FILE * fp;
  fp = fopen(schema->file_name, "r");

  if (fp == NULL) {
    printf("WARNING: JSON file %s not found\n", schema->file_name);
    return 1;
  }

  int MAX_FILE_SIZE = 1024 * 1024;
  char * json_schema = malloc(sizeof(char) * MAX_FILE_SIZE);

  fread(json_schema, sizeof(char), MAX_FILE_SIZE, fp);

  char errorbuffer[1024];
  yajl_val node = yajl_tree_parse(json_schema, errorbuffer, sizeof(errorbuffer));

  schema->payload = node;

  if (node == NULL) {
    printf("WARNING: JSON file %s not valid\n", schema->file_name);
    return 1;
  }

  return 0;
}

void print_response(const ail_response_t response) {
  struct response_chunk * current = response;
  while (current) {
      printf("%s", current->chunk);
      current = current->next;
  }
}