/* -*- c++ -*- */
/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "shader_cache.h"
#include "ir_serialize.h"


/**
 * Serializes gl_shader structure, writes shader header
 * information and exec_list of instructions
 */
extern "C" char *
mesa_shader_serialize(struct gl_shader *shader, size_t *size)
{
   *size = 0;

   memory_writer blob;

   int32_t start_pos = blob.position();
   uint32_t shader_data_len = 0;
   uint32_t shader_type = shader->Type;

   blob.write_uint32_t(shader_data_len);
   blob.write_uint32_t(shader_type);

   blob.write(shader, sizeof(struct gl_shader));

   /* dump all shader instructions */
   serialize_list(shader->ir, blob);

   shader_data_len = blob.position() -
      start_pos - sizeof(shader_data_len);
   blob.overwrite(&shader_data_len, sizeof(shader_data_len), start_pos);

   return blob.release_memory(size);
}


/**
 * helper structure for hash serialization, hash size is
 * counted to item_count during serialization
 */
struct hash_serialize_data
{
   hash_serialize_data(void *memory_writer) :
      writer(memory_writer),
      item_count(0) { }

   void *writer;
   uint32_t item_count;
};


static void
serialize_hash(const void *key, void *data, void *closure)
{
   hash_serialize_data *s_data = (hash_serialize_data *) closure;
   memory_writer *blob = (memory_writer *) s_data->writer;

   uint32_t value = ((intptr_t)data);

   blob->write_string((char *)key);
   blob->write_uint32_t(value);

   s_data->item_count++;
}


static void
serialize_hash_table(struct string_to_uint_map *map, memory_writer *blob)
{
   struct hash_serialize_data data(blob);
   int32_t pos = blob->position();
   blob->write_uint32_t(data.item_count);

   map->iterate(serialize_hash, &data);

   blob->overwrite(&data.item_count, sizeof(data.item_count), pos);
}


static void
serialize_uniform_storage(gl_uniform_storage *uni, memory_writer &blob)
{
   blob.write(uni, sizeof(gl_uniform_storage));
   blob.write_string(uni->name);

   /* note, type is not serialized, it is resolved during parsing */

   /* how many elements (1 if not array) * how many components in the type */
   const unsigned elements = MAX2(1, uni->array_elements);
   uint32_t size = elements * MAX2(1, uni->type->components());

   blob.write_uint32_t(size);
}


/**
 * Features not currently supported by the cache.
 */
static bool
supported_by_cache(struct gl_shader_program *prog)
{
   /* No geometry shader support. */
   if (prog->_LinkedShaders[MESA_SHADER_GEOMETRY])
      return false;

   /* No uniform block support. */
   if (prog->NumUniformBlocks > 0)
      return false;

   /* No transform feedback support. */
   if (prog->LinkedTransformFeedback.NumVarying > 0)
      return false;

   return true;
}


/**
 * Serialize gl_shader_program structure
 */
extern "C" char *
mesa_program_serialize(struct gl_shader_program *prog, size_t *size)
{
   if (!supported_by_cache(prog))
      return NULL;

   memory_writer blob;
   blob.write(&cache_validation_data, sizeof(cache_validation_data));

   GET_CURRENT_CONTEXT(ctx);

   blob.write_string(mesa_get_shader_cache_magic());
   blob.write_string((const char *)ctx->Driver.GetString(ctx, GL_VENDOR));
   blob.write_string((const char *)ctx->Driver.GetString(ctx, GL_RENDERER));

   blob.write(prog, sizeof(gl_shader_program));

   /* hash tables */
   serialize_hash_table(prog->AttributeBindings, &blob);
   serialize_hash_table(prog->FragDataBindings, &blob);
   serialize_hash_table(prog->FragDataIndexBindings, &blob);
   serialize_hash_table(prog->UniformHash, &blob);

   /* uniform storage */
   if (prog->UniformStorage) {
      for (unsigned i = 0; i < prog->NumUserUniformStorage; i++)
         serialize_uniform_storage(&prog->UniformStorage[i], blob);
   }

   uint8_t shader_amount = 0;
   unsigned shader_amount_pos = blob.position();
   blob.write_uint8_t(shader_amount);

   /* _LinkedShaders IR */
   for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++) {
      size_t sha_size = 0;

      if (!prog->_LinkedShaders[i])
         continue;

      /* Set used GLSL version and IsES flag from gl_shader_program,
       * this is required when deserializing the data.
       */
      prog->_LinkedShaders[i]->Version = prog->Version;
      prog->_LinkedShaders[i]->IsES = prog->IsES;

      char *data = mesa_shader_serialize(prog->_LinkedShaders[i], &sha_size);

      if (!data)
         return NULL;

      shader_amount++;

      /* index in _LinkedShaders list + shader blob */
      if (data) {
         blob.write_uint32_t(i);
         blob.write(data, sha_size);
         free(data);
      }
   }

   blob.overwrite(&shader_amount, sizeof(shader_amount), shader_amount_pos);

   *size = blob.position();
   return blob.release_memory(size);
}
