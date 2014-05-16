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
#include "ir_deserializer.h"
#include "main/context.h"
#include "main/shaderapi.h"

#if 0
static struct gl_program_parameter_list*
deserialize_program_parameters(memory_map &map)
{
   struct gl_program_parameter_list *list = _mesa_new_parameter_list();
   uint8_t par_amount = map.read_uint8_t();

   if (par_amount == 0)
      return list;

   for (unsigned i = 0; i < par_amount; i++) {

      struct gl_program_parameter par;
      gl_constant_value values[4];

      char *name = map.read_string();
      map.read(&par, sizeof(struct gl_program_parameter));
      map.read(values, 4 * sizeof(gl_constant_value));

      _mesa_add_parameter(list, par.Type, name, par.Size, par.DataType,
                          values, par.StateIndexes);
   }
   list->StateFlags = map.read_uint32_t();

   return list;
}


/**
 * gl_program contains post-link data populated by the driver
 */
static bool
deserialize_gl_program(struct gl_shader *shader, memory_map &map)
{
   map.read(shader->Program, sizeof(struct gl_program));
   char *str = map.read_string();
   shader->Program->String = (GLubyte*) _mesa_strdup(str);

   shader->Program->Parameters = deserialize_program_parameters(map);

   if (map.errors())
      return false;

   return true;
}
#endif

static bool
read_hash_table(struct string_to_uint_map *hash, memory_map *map)
{
   if (map->errors())
      return false;

   uint32_t size = map->read_uint32_t();

   for (unsigned i = 0; i < size; i++) {

      char *key = map->read_string();
      uint32_t value = map->read_uint32_t();

      /* put() adds +1 bias on the value (see hash_table.h), this
       * is taken care here when reading
       */
      hash->put(value-1, key);

      /* break out in case of read errors */
      if (map->errors())
         return false;
   }
   return true;
}


static void
read_uniform_storage(void *mem_ctx, gl_uniform_storage *uni, memory_map &map)
{
   map.read(uni, sizeof(gl_uniform_storage));

   char *name = map.read_string();
   uni->name = _mesa_strdup(name);

   /* type is resolved later */
   uni->type = NULL;
   uni->driver_storage = NULL;

   uint32_t size = map.read_uint32_t();

   uni->storage = rzalloc_array(mem_ctx, union gl_constant_value, size);

   /* initialize to zero for now, initializers will be propagated later */
   memset(uni->storage, 0, size * sizeof(union gl_constant_value));

   /* driver uniform storage gets generated and propagated later */
   uni->driver_storage = NULL;
   uni->num_driver_storage = 0;
}


static ir_variable *
search_var(struct exec_list *list, const char *name)
{
   foreach_list_safe(node, list) {
      ir_variable *var = ((ir_instruction *) node)->as_variable();
      if (var && strcmp(name, var->name) == 0)
         return var;
   }
   return NULL;
}


/**
 * Resolve glsl_types for uniform_storage
 */
static void
resolve_uniform_types(struct gl_shader_program *prog, struct gl_shader *sha)
{
   /* for each storage, find corresponding uniform from the shader */
   for (unsigned i = 0; i < prog->NumUserUniformStorage; i++) {
      ir_variable *var = search_var(sha->ir, prog->UniformStorage[i].name);

      if (var) {
         /* for arrays, uniform storage type contains the element type */
         if (var->type->is_array())
            prog->UniformStorage[i].type = var->type->element_type();
         else
            prog->UniformStorage[i].type = var->type;
      }
   }
}


/* read and serialize a gl_shader */
static gl_shader *
read_shader(void *mem_ctx, memory_map &map, ir_deserializer &s)
{
   struct gl_shader *shader = NULL;
   struct gl_program *prog = NULL;
   gl_shader_stage stage;
   GET_CURRENT_CONTEXT(ctx);

   uint32_t shader_size = map.read_uint32_t();
   uint32_t type = map.read_uint32_t();

   GLuint name;
   GLint refcount;

   /* Useful information for debugging. */
   (void) shader_size;

   /* verify that type is supported */
   switch (type) {
      case GL_VERTEX_SHADER:
      case GL_FRAGMENT_SHADER:
      case GL_GEOMETRY_SHADER:
         break;
      default:
         goto error_deserialize;
   }

   shader = ctx->Driver.NewShader(NULL, 0, type);

   if (!shader)
      return NULL;

   name = shader->Name;
   refcount = shader->RefCount;

   /* Reading individual fields and structs would slow us down here. This is
    * slightly dangerous though and we need to take care to initialize any
    * pointers properly.
    */
   map.read(shader, sizeof(struct gl_shader));

   /* verify that type from header matches */
   if (shader->Type != type)
      goto error_deserialize;

   /* Set correct name and refcount. */
   shader->Name = name;
   shader->RefCount = refcount;

   /* clear all pointer fields, only data preserved */
   shader->Label = NULL;
   shader->Source = NULL;
   shader->Program = NULL;
   shader->InfoLog = ralloc_strdup(mem_ctx, "");
   shader->UniformBlocks = NULL;
   shader->ir = NULL;
   shader->symbols = NULL;

   stage = _mesa_shader_enum_to_shader_stage(shader->Type);

   prog =
      ctx->Driver.NewProgram(ctx, _mesa_shader_stage_to_program(stage),
                             shader->Name);

   if (!prog)
      goto error_deserialize;

   _mesa_reference_program(ctx, &shader->Program, prog);

   /* IR tree */
   if (!s.deserialize(mem_ctx, shader, &map))
      goto error_deserialize;

   return shader;

error_deserialize:

   if (shader)
      ctx->Driver.DeleteShader(ctx, shader);

   return NULL;
}


/**
 * Deserialize gl_shader structure
 */
extern "C" struct gl_shader *
mesa_shader_deserialize(void *mem_ctx, void *data, size_t size)
{
   memory_map map;
   ir_deserializer s;
   map.map(data, size);
   return read_shader(mem_ctx, map, s);
}


static bool
validate_binary_program(struct gl_shader_program *prog, memory_map &map)
{
   uint32_t data[num_cache_validation_data_items];
   map.read(&data, sizeof(cache_validation_data));

   /* validation data (common struct sizes) must match */
   if (memcmp(&data, cache_validation_data, sizeof(cache_validation_data)))
      return false;

   char *cache_magic_id = map.read_string();
   char *cache_vendor = map.read_string();
   char *cache_renderer = map.read_string();

   const char *magic = mesa_get_shader_cache_magic();

   GET_CURRENT_CONTEXT(ctx);

   const char *mesa_vendor =
      (const char *) ctx->Driver.GetString(ctx, GL_VENDOR);
   const char *mesa_renderer =
      (const char *) ctx->Driver.GetString(ctx, GL_RENDERER);

   /* check if cache was created with another driver */
   if ((strcmp(mesa_vendor, cache_vendor)) ||
      (strcmp(mesa_renderer, cache_renderer)))
         return false;

   /* check against different version of mesa */
   if (strcmp(cache_magic_id, magic))
      return false;

   return true;
}


static int
deserialize_program(struct gl_shader_program *prog, memory_map &map)
{
   GET_CURRENT_CONTEXT(ctx);

   if (validate_binary_program(prog, map) == false)
      return MESA_SHADER_DESERIALIZE_VERSION_ERROR;

   struct gl_shader_program tmp_prog;

   map.read(&tmp_prog, sizeof(gl_shader_program));

   /* Cache does not support compatibility extensions
    * like ARB_ES3_compatibility (yet).
    */
   if (_mesa_is_desktop_gl(ctx) && tmp_prog.IsES)
      return MESA_SHADER_DESERIALIZE_READ_ERROR;

   prog->Type = tmp_prog.Type;
   prog->Version = tmp_prog.Version;
   prog->IsES = tmp_prog.IsES;
   prog->NumUserUniformStorage = tmp_prog.NumUserUniformStorage;
   prog->NumUniformRemapTable = tmp_prog.NumUniformRemapTable;
   prog->LastClipDistanceArraySize = tmp_prog.LastClipDistanceArraySize;
   prog->FragDepthLayout = tmp_prog.FragDepthLayout;

   prog->UniformStorage = NULL;
   prog->Label = NULL;

   prog->UniformHash = new string_to_uint_map;

   /* these already allocated by _mesa_init_shader_program */
   read_hash_table(prog->AttributeBindings, &map);
   read_hash_table(prog->FragDataBindings, &map);
   read_hash_table(prog->FragDataIndexBindings, &map);

   read_hash_table(prog->UniformHash, &map);

   if (map.errors())
      return MESA_SHADER_DESERIALIZE_READ_ERROR;

   memcpy(&prog->Geom, &tmp_prog.Geom, sizeof(prog->Geom));
   memcpy(&prog->Vert, &tmp_prog.Vert, sizeof(prog->Vert));

   /* uniform storage */
   prog->UniformStorage = rzalloc_array(prog, struct gl_uniform_storage,
                                        prog->NumUserUniformStorage);

   for (unsigned i = 0; i < prog->NumUserUniformStorage; i++)
      read_uniform_storage(prog, &prog->UniformStorage[i], map);

   prog->UniformRemapTable =
      rzalloc_array(prog, gl_uniform_storage *, prog->NumUniformRemapTable);

   /* assign remap entries from UniformStorage */
   for (unsigned i = 0; i < prog->NumUserUniformStorage; i++) {
      unsigned entries = MAX2(1, prog->UniformStorage[i].array_elements);
      for (unsigned j = 0; j < entries; j++)
         prog->UniformRemapTable[prog->UniformStorage[i].remap_location + j] =
            &prog->UniformStorage[i];
   }

   /* how many linked shaders does the binary contain */
   uint8_t shader_amount = map.read_uint8_t();

   /* use same deserializer to have same type_hash across shader stages */
   ir_deserializer s;

   /* init list, cache can contain only some shader types */
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++)
      prog->_LinkedShaders[i] = NULL;

   /* Only reading error or error constructing gl_program can change this. */
   prog->LinkStatus = true;

   /* read _LinkedShaders */
   for (unsigned i = 0; i < shader_amount; i++) {
      uint32_t index = map.read_uint32_t();

      struct gl_shader *sha = read_shader(prog, map, s);

      if (!sha) {
         prog->LinkStatus = false;
         return MESA_SHADER_DESERIALIZE_READ_ERROR;
      }

      resolve_uniform_types(prog, sha);

      _mesa_reference_shader(ctx, &prog->_LinkedShaders[index], sha);

      struct gl_program *linked_prog = ctx->Driver.GetProgram(ctx, prog, sha);
      if (linked_prog) {
         _mesa_copy_linked_program_data((gl_shader_stage) index,
                                        prog, linked_prog);
         _mesa_reference_program(ctx, &sha->Program, linked_prog);
         _mesa_reference_program(ctx, &linked_prog, NULL);
      }

   }

   /* set default values for uniforms that have initializer */
   link_set_uniform_initializers(prog);

   prog->_Linked = GL_TRUE;

   return 0;
}

/**
 * Deserialize gl_shader_program structure
 */
extern "C" int
mesa_program_deserialize(struct gl_shader_program *prog, const GLvoid *data,
                         size_t size)
{
   memory_map map;
   map.map((const void*) data, size);
   return deserialize_program(prog, map);
}
