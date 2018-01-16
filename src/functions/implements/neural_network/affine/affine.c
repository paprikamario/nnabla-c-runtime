// Copyright (c) 2017 Sony Corporation. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <nnablart/functions.h>

#include <assert.h>
#include <memory.h>

#include "../../../utilities.h"

struct affine_impl {
  affine_config_t config;

  rt_variable_t *input;
  rt_variable_t *weight;
  rt_variable_t *bias;
  rt_variable_t *output;

  int count;
  int input_width;
  int output_width;
};
typedef struct affine_impl affine_impl_t;

static inline int product(const int *array, int begin, int end) {
  int product = 1;
  const int *it = array + begin;
  while (it != array + end) {
    product *= *it++;
  }
  return product;
}

// Affine
void allocate_affine_config(rt_function_t *f) {
  assert(f->num_of_inputs == 2 || f->num_of_inputs == 3);
  assert(f->num_of_outputs == 1);
  void *buf = realloc(f->config, sizeof(affine_impl_t));
  if (!buf) {
    buf = malloc(sizeof(affine_impl_t));
    memcpy(buf, f->config, sizeof(affine_config_t));
    free(f->config);
    f->config = buf;
  }
  affine_impl_t *const pimpl = buf;

  pimpl->input = f->inputs[0];
  pimpl->weight = f->inputs[1];
  pimpl->bias = f->num_of_inputs == 3 ? f->inputs[2] : NULL;
  pimpl->output = f->outputs[0];

  const int base_axis = pimpl->config.base_axis;
  pimpl->count = product(pimpl->input->shape.data, 0, base_axis);
  pimpl->input_width = product(pimpl->input->shape.data, base_axis, pimpl->input->shape.size);
  pimpl->output_width = product(pimpl->output->shape.data, base_axis, pimpl->output->shape.size);
  assert(calc_shape_size(f->outputs[0]->shape) == pimpl->count * pimpl->output_width);
}

void free_affine_config(rt_function_t *f) {
  (void) realloc(f->config, sizeof(affine_config_t)); // can be omitted
}

static inline void clear(rt_variable_setter setter, rt_variable_t *list, int length) {
  if (!setter) {
    memset(list->data, 0, sizeof(float) * length);
  } else {
    int i;
    for (i = 0; i < length; ++i) {
      setter(list, i, 0);
    }
  }
}

static inline void poke(rt_variable_setter setter, rt_variable_t *list, int position, float value) {
  if (!setter) {
    ((float *)list->data)[position] = value;
  } else {
    setter(list, position, value);
  }
}

static inline float peek(rt_variable_getter getter, rt_variable_t *list, int position) {
  if (!getter) {
    return ((float *)list->data)[position];
  } else {
    return getter(list, position);
  }
}

void exec_affine(rt_function_t *f) {
  affine_impl_t *const pimpl = f->config;
  const int allFloat = pimpl->input->type == NN_DATA_TYPE_FLOAT &&
      pimpl->output->type == NN_DATA_TYPE_FLOAT &&
      pimpl->weight->type == NN_DATA_TYPE_FLOAT &&
      (!pimpl->bias || pimpl->bias->type == NN_DATA_TYPE_FLOAT);
  const rt_variable_getter get_input = allFloat ? NULL : select_getter(pimpl->input);
  const rt_variable_getter get_weight = allFloat ? NULL : select_getter(pimpl->weight);
  const rt_variable_getter get_bias = allFloat ? NULL : select_getter(pimpl->bias);
  const rt_variable_getter get_output = allFloat ? NULL : select_getter(pimpl->output);
  const rt_variable_setter set_output = allFloat ? NULL :  select_setter(pimpl->output);
  int i, j, k; // Iterators.

  // Clear output
  clear(set_output, pimpl->output, pimpl->count * pimpl->output_width);

  for (k = 0; k < pimpl->count; k++) {
    int output_offset = k * pimpl->output_width;
    int input_offset = k * pimpl->input_width;

    // Weight
    for (j = 0; j < pimpl->input_width; j++) {
      int ipos = input_offset + j;
      int weight_offset = j * pimpl->output_width;

      float u = peek(get_input, pimpl->input, ipos);
      for (i = 0; i < pimpl->output_width; i++) {
        int opos = output_offset + i;
        int wpos = weight_offset + i;

        float w = peek(get_weight, pimpl->weight, wpos);
        float value = peek(get_output, pimpl->output, opos);
        poke(set_output, pimpl->output, opos, value + u * w);
      }
    }

    // Bias
    if (pimpl->bias) {
      for (i = 0; i < pimpl->output_width; i++) {
        int opos = output_offset + i;
        int bpos = i;

        float b = peek(get_bias, pimpl->bias, bpos);
        float value = peek(get_output, pimpl->output, opos);
        poke(set_output, pimpl->output, opos, value + b);
      }
    }
  }
}
