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

#pragma once
#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>
#include <string.h>
#include "ralloc.h"

#ifdef __cplusplus

/**
 * Helper class to read data
 *
 * Class reads data from user given memory.
 */
class memory_map
{
public:
   memory_map() :
      error(false),
      cache_size(0),
      cache_mmap(NULL),
      cache_mmap_p(NULL)
   {
      /* only used by read_string() */
      mem_ctx = ralloc_context(NULL);
   }

   /* read from memory */
   void map(const void *memory, size_t size)
   {
      cache_mmap_p = cache_mmap = (char *) memory;
      cache_size = size;
   }

   ~memory_map() {
      ralloc_free(mem_ctx);
   }

   /* move read pointer forward */
   inline void ffwd(int len)
   {
      cache_mmap_p += len;
   }

   inline void jump(unsigned pos)
   {
      cache_mmap_p = cache_mmap + pos;
   }

   /**
    * safety check to avoid reading over cache_size,
    * returns bool if it is safe to continue reading
    */
   bool safe_read(unsigned size)
   {
      if (position() + size > cache_size)
         error = true;
      return !error;
   }

   /* position of read pointer */
   inline uint32_t position()
   {
      return cache_mmap_p - cache_mmap;
   }

   inline char *read_string()
   {
      uint32_t len = read_uint32_t();

      /* NULL pointer is supported */
      if (len == 0)
         return NULL;

      /* verify that last character is terminator */
      if (*(cache_mmap_p + len - 1) != '\0') {
         error = true;
         return NULL;
      }

      char *str = ralloc_array(mem_ctx, char, len);
      memcpy(str, cache_mmap_p, len);
      ffwd(len);
      return str;
   }

/**
 * read functions per type
 */
#define DECL_READER(type) type read_ ##type () {\
   if (!safe_read(sizeof(type)))\
      return 0;\
   ffwd(sizeof(type));\
   return *(type *) (cache_mmap_p - sizeof(type));\
}

   DECL_READER(int32_t);
   DECL_READER(int64_t);
   DECL_READER(uint8_t);
   DECL_READER(uint32_t);

   inline uint8_t read_bool()
   {
      return read_uint8_t();
   }

   inline void read(void *dst, size_t size)
   {
      if (!safe_read(size))
         return;
      memcpy(dst, cache_mmap_p, size);
      ffwd(size);
   }

   /* total size of mapped memory */
   inline int32_t size()
   {
      return cache_size;
   }

   inline bool errors()
   {
      return error;
   }

private:

   void *mem_ctx;

   /* if errors have occured during reading */
   bool error;

   unsigned cache_size;
   char *cache_mmap;
   char *cache_mmap_p;
};
#endif /* ifdef __cplusplus */

#endif /* MEMORY_MAP_H */
