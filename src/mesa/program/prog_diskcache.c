/* -*- c -*- */
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

#include <sys/stat.h>
#include "shader_cache.h"
#include "prog_diskcache.h"

static int
mesa_mkdir_cache(const char *path)
{
   char *copy = _mesa_strdup(path);
   char *dir = strtok(copy, "/");
   char *current = ralloc_strdup(NULL, "/");
   int result = 0;

   /* As example loop iterates and calls mkdir for each token
    * separated by '/' in "/home/yogsothoth/.cache/mesa".
    */
   while (dir) {
      ralloc_strcat(&current, dir);

      result = _mesa_mkdir(current);

      if (result != 0 && result != EEXIST)
         return -1;

      ralloc_strcat(&current, "/");
      dir = strtok(NULL, "/");
   }

   ralloc_free(current);
   free(copy);

   return 0;
}


int
mesa_program_diskcache_init(struct gl_context *ctx)
{
   const char *tmp = "/tmp", *cache_root = NULL;
   int result = 0;

   cache_root = _mesa_getenv("XDG_CACHE_DIR");
   if (!cache_root)
      cache_root = _mesa_getenv("HOME");
   if (!cache_root)
      cache_root = tmp;

   asprintf(&ctx->BinaryCachePath, "%s/.cache/mesa", cache_root);

   struct stat stat_info;
   if (stat(ctx->BinaryCachePath, &stat_info) != 0)
      result = mesa_mkdir_cache(ctx->BinaryCachePath);

   if (result == 0)
      ctx->BinaryCacheActive = true;
   else
      ctx->BinaryCacheActive = false;

   return result;
}


static uint32_t
checksum(const char *src)
{
   uint32_t sum = _mesa_str_checksum(src);
   unsigned i;

   /* Add some sugar on top (borrowed from brw_state_cache). This is meant
    * to catch cache collisions when there are only small changes in the
    * source such as mat3 -> mat4 in a type for example.
    */
   for (i = 0; i < strlen(src); i++) {
      sum ^= (uint32_t) src[i];
      sum = (sum << 5) | (sum >> 27);
   }

   return sum;
}


/**
 * Attempt to generate unique key for a gl_shader_program.
 * TODO - this should be stronger and be based on some of the
 * gl_shader_program content, not just sources.
 */
static char *
generate_key(struct gl_shader_program *prog)
{
   char *key = ralloc_strdup(prog, "");
   for (unsigned i = 0; i < prog->NumShaders; i++) {

      /* No source, no key. */
      if (!prog->Shaders[i]->Source)
         return NULL;

      /* At least some content required. */
      if (strcmp(prog->Shaders[i]->Source, "") == 0)
         return NULL;

      uint64_t sum = checksum(prog->Shaders[i]->Source);

      char tmp[32];
      _mesa_snprintf(tmp, 32, "%lu", sum);

      ralloc_strcat(&key, tmp);
   }

   /* Key needs to have enough content. */
   if (strlen(key) < 7) {
      ralloc_free(key);
      key = NULL;
   }

   return key;
}


/**
 * Cache gl_shader_program to disk
 */
int
mesa_program_diskcache_cache(struct gl_shader_program *prog)
{
   int result = -1;
   struct stat stat_info;
   char *key;

   GET_CURRENT_CONTEXT(ctx);

   key = generate_key(prog);

   if (!key)
      return -1;

   char *shader_path =
      ralloc_asprintf(NULL, "%s/%s.bin", ctx->BinaryCachePath, key);

   /* Collision, do not attempt to overwrite. */
   if (stat(shader_path, &stat_info) == 0)
      goto cache_epilogue;

   size_t size = 0;
   char *data = mesa_program_serialize(prog, &size);

   if (!data)
      goto cache_epilogue;

   FILE *out = fopen(shader_path, "w+");

   if (!out)
      goto cache_epilogue;

   fwrite(data, size, 1, out);
   fclose(out);
   free(data);
   result = 0;

cache_epilogue:
   ralloc_free(shader_path);
   ralloc_free(key);
   return result;
}


/**
 * Fill gl_shader_program from cache if found
 */
int
mesa_program_diskcache_find(struct gl_shader_program *prog)
{
   int result = 0;
   char *key;

   GET_CURRENT_CONTEXT(ctx);

   /* Do not use diskcache when program relinks. Relinking is not
    * currently supported due to the way how cache key gets generated.
    * We would need to modify key generation to take account hashtables
    * and possible other data in gl_shader_program to catch changes in
    * the data used as input for linker (resulting in a different program
    * with same sources).
    */
   if (prog->_Linked)
      return -1;

   key = generate_key(prog);

   if (!key)
      return -1;

   char *shader_path =
      ralloc_asprintf(NULL, "%s/%s.bin", ctx->BinaryCachePath, key);

   result = mesa_program_load(prog, shader_path);

   ralloc_free(shader_path);
   ralloc_free(key);

   return result;
}
