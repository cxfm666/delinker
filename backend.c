#include <stdio.h>
#include <string.h>
#include "backend.h"
#include "ll.h"
#include "config.h"

#define DECLARE_BACKEND_INIT_FUNC(_x) extern int _x##_init()
#define BACKEND_INIT_FUNC(_x) _x##_init

DECLARE_BACKEND_INIT_FUNC(mz);
DECLARE_BACKEND_INIT_FUNC(pe);
DECLARE_BACKEND_INIT_FUNC(elf32);
DECLARE_BACKEND_INIT_FUNC(elf64);


typedef int (*backend_init_func)(void);
static backend_init_func backend_table[] = 
{
	BACKEND_INIT_FUNC(mz),
	BACKEND_INIT_FUNC(pe),
	BACKEND_INIT_FUNC(elf32),
	BACKEND_INIT_FUNC(elf64),
};

#define BACKEND_INIT(_x) backend_table[_x]
#define BACKEND_COUNT sizeof(backend_table)/sizeof(void*)

static int num_backends;
static backend_ops* backend[BACKEND_COUNT] = {0};

static int backend_iter;

int backend_init(void)
{
	// here we use the macro BACKEND_COUNT to know how many backends exist. This is different
	// from num_backends which is how many backends have been registered (initialized successfully).
	for (int i=0; i < BACKEND_COUNT; i++)
	{
		if (BACKEND_INIT(i))
			BACKEND_INIT(i)();
	}

	return 0;
}

void backend_register(backend_ops* be)
{
   if (num_backends >= BACKEND_COUNT)
   {
      printf("Can't accept any more backends - sorry, we're full! (MAX_BACKENDS=%lu)\n", BACKEND_COUNT);
      return;
   }

   if (!be->format)
   {
      printf("You must implement the format() function\n");
      return;
   }

	printf("registering backend %s\n", be->name());

   backend[num_backends++] = be;
   //printf("num backends %i\n", num_backends);
}

static int cmp_by_name(void* a, const void* b)
{
	backend_symbol* s = (backend_symbol*)a;
	const char* name = (const char*)b;
	return strcmp(s->name, name);
}

backend_type backend_lookup_target(const char* name)
{
	if (!name)
   	return OBJECT_TYPE_NONE;

	// iterate through all known backends, comparing the string. When we find a match, convert the name to the correct type
   for (int i=0; i < num_backends; i++)
   {
		//printf("Looking up %i\n", i);
		//printf("Found backend %s\n", backend[i]->name());
      if (backend[i]->name && strcmp(backend[i]->name(), name) == 0)
			return backend[i]->format();
   }
   return OBJECT_TYPE_NONE;
}

const char* backend_get_first_target(void)
{
	backend_iter = 0;

	if (num_backends == 0)
		return NULL;

	return backend[backend_iter++]->name();
}

const char* backend_get_next_target(void)
{
	if (backend_iter < num_backends)
		return backend[backend_iter++]->name();

	return NULL;
}

const char* backend_symbol_type_to_str(backend_symbol_type t)
{
	switch(t)
	{
	case SYMBOL_TYPE_NONE:
		return "none";
	case SYMBOL_TYPE_FILE:
		return "file";
	case SYMBOL_TYPE_SECTION:
		return "section";
	case SYMBOL_TYPE_FUNCTION:
		return "function";
	case SYMBOL_TYPE_OBJECT:
		return "object";
	default:
		return "Unknown";
	}
	return "Unknown";
}

backend_object* backend_create(void)
{
   backend_object* obj = (backend_object*)calloc(1, sizeof(backend_object));
   return obj;
}

backend_object* backend_read(const char* filename)
{
   //printf("backend_read\n");
   // run through all backends until we find one that recognizes the format and returns an object
   for (int i=0; i < num_backends; i++)
   {
      backend_object* obj = backend[i]->read(filename);
      if (obj)
         return obj;
   }
   return 0;
}

int backend_write(backend_object* obj)
{
	//printf("backend_write looking for type %i\n", obj->type);
   // run through all backends until we find one that matches the output format
   for (int i=0; i < num_backends; i++)
   {
      if (backend[i]->format() == obj->type)
      {
         if (!backend[i]->write)
            return -2;

			//printf("Using backend %i\n", i);
         return backend[i]->write(obj, obj->name);
      }
   }

   return -1;
}

void backend_set_filename(backend_object* obj, const char* name)
{
	obj->name = strdup(name);
}

void backend_set_type(backend_object* obj, backend_type t)
{
	//printf("setting backend type to %i\n", t);
   obj->type = t;
}

backend_type backend_get_type(backend_object* obj)
{
	return obj->type;
}

void backend_set_arch(backend_object* obj, backend_arch a)
{
   obj->arch = a;
}

backend_arch backend_get_arch(backend_object* obj)
{
   return obj->arch;
}

void backend_set_entry_point(backend_object* obj, unsigned long addr)
{
   if (config.verbose)
	   fprintf(stderr, "Setting entry point to 0x%lx\n", addr);
	obj->entry = addr;
}

unsigned long backend_get_entry_point(backend_object* obj)
{
	return obj->entry;
}

static void dump_symbol_table(backend_object* obj)
{
   if (!obj || !obj->symbol_table)
      return;

   for (const list_node* iter=ll_iter_start(obj->symbol_table); iter != NULL; iter=iter->next)
	{
		backend_symbol *bs = (backend_symbol*)iter->val;
		//printf("** %s 0x%lx\n", bs->name, bs->val);
	}
}

unsigned int backend_symbol_count(backend_object* obj)
{
   if (obj && obj->symbol_table)
      return ll_size(obj->symbol_table);
   else
      return 0;
}

backend_symbol* backend_add_symbol(backend_object* obj, const char* name, unsigned long val,
	backend_symbol_type type, unsigned long size, unsigned int flags, backend_section* sec)
{
	// can't add a symbol if there is no backend object
	if (!obj)
		return NULL;

   if (!obj->symbol_table)
      obj->symbol_table = ll_init();

	if (!name)
		name = "!";

   backend_symbol* s = (backend_symbol*)malloc(sizeof(backend_symbol));
   s->name = strdup(name);
   s->val = val;
   s->type = type;
	s->size = size;
   s->flags = flags;
   s->section = sec;
	s->src = NULL;

	ll_add(obj->symbol_table, s);
   //printf("There are %i symbols\n", backend_symbol_count(obj));
   return s;
}

backend_symbol* backend_get_first_symbol(backend_object* obj)
{
   if (!obj->symbol_table)
      return NULL;
   obj->iter_symbol = ll_iter_start(obj->symbol_table);
   return (backend_symbol*)obj->iter_symbol->val;
}

backend_symbol* backend_get_next_symbol(backend_object* obj)
{
   obj->iter_symbol = obj->iter_symbol->next; 
   if (obj->iter_symbol)
      return (backend_symbol*)obj->iter_symbol->val;
   return NULL;
}

backend_symbol* backend_find_symbol_by_val(backend_object* obj, unsigned long val)
{
	backend_symbol* bs;

   if (!obj || !obj->symbol_table)
      return NULL;

   for (const list_node* iter=ll_iter_start(obj->symbol_table); iter != NULL; iter=iter->next)
	{
		bs = (backend_symbol*)iter->val;
		//printf("** %s 0x%lx\n", bs->name, bs->val);
		if ((bs->size == 0 && val == bs->val) || (val >= bs->val && val < (bs->val + bs->size)))
			return bs;
	}

	return NULL;
}

backend_symbol* backend_find_symbol_by_name(backend_object* obj, const char* name)
{
   if (!obj || !obj->symbol_table)
      return NULL;

   for (const list_node* iter=ll_iter_start(obj->symbol_table); iter != NULL; iter=iter->next)
	{
		backend_symbol *bs = (backend_symbol*)iter->val;
		//printf("++ %s\n", bs->name);
		if (bs->name && strcmp(bs->name, name) == 0)
			return bs;
	}

	return NULL;
}

backend_symbol* backend_find_symbol_by_index(backend_object* obj, unsigned int index)
{
   if (!obj || !obj->symbol_table)
      return NULL;

   const list_node* iter=ll_iter_start(obj->symbol_table);
	for (unsigned int i=0; i < index; i++)
	{
		if (!iter)
			return NULL;
		//backend_symbol *bs = (backend_symbol*)iter->val;
		//printf("++ %s\n", bs->name);
		iter=iter->next;
	}
	if (iter)
	{
		//backend_symbol *bs = (backend_symbol*)iter->val;
		//printf("++ %s\n", bs->name);
		return (backend_symbol*)iter->val;
	}

	return NULL;
}

backend_symbol* backend_find_symbol_by_val_type(backend_object* obj, unsigned long val, backend_symbol_type type)
{
	backend_symbol* bs;

	if (!obj || !obj->symbol_table)
		return NULL;

	for (const list_node* iter=ll_iter_start(obj->symbol_table); iter != NULL; iter=iter->next)
	{
		bs = (backend_symbol*)iter->val;
//			printf("Found symbol %s (%lu)\n", bs->name, bs->val);
		if (((bs->size == 0 && val == bs->val) || (val >= bs->val && val < (bs->val + bs->size))) && bs->type == type)
			return bs;
	}

	return NULL;
}

backend_symbol* backend_find_nearest_symbol(backend_object* obj, unsigned long val)
{
	backend_symbol *bs;
	backend_symbol *prevbs=NULL;

	if (!obj || !obj->symbol_table)
		return NULL;

	for (const list_node* iter=ll_iter_start(obj->symbol_table); iter != NULL; iter=iter->next)
	{
		bs = (backend_symbol*)iter->val;
		if (bs->val > val)
			return prevbs;
		prevbs = bs;
	}

	return NULL;
}

backend_symbol* backend_merge_symbol(backend_object* obj, backend_symbol *sym)
{
	backend_symbol *prev=NULL;

	if (!obj || !obj->symbol_table)
		return NULL;

	// find the insertion point
	for (list_node* iter=(list_node*)ll_iter_start(obj->symbol_table); iter != NULL; iter=iter->next)
	{
		if (iter->val == sym)
		{
			if (!prev)
				return sym;

			// there may be empty space between the functions, so we can't just add the sizes together
			printf("Merging into %s: oldsize=%lu newsize=%lu\n", prev->name, prev->size, (sym->val + sym->size) - prev->val);
			prev->size = (sym->val + sym->size) - prev->val;
			printf("Removing %s\n", sym->name);
			backend_symbol *old = (backend_symbol *)ll_remove(obj->symbol_table, sym->name, cmp_by_name);
			if (old)
			{
				free(old->name);
				old->name = NULL;
				free(old);
			}
			return prev;
		}
		prev = (backend_symbol *)iter->val;
	}
	return NULL;
}

backend_symbol* backend_split_symbol(backend_object* obj, backend_symbol *sym, const char* name, unsigned long val, backend_symbol_type type, unsigned int flags)
{
	if (!obj || !obj->symbol_table)
		return NULL;

	// find the insertion point
	for (list_node* iter=(list_node*)ll_iter_start(obj->symbol_table); iter != NULL; iter=iter->next)
	{
		if (iter->val == sym)
		{
			unsigned int newsize;
			backend_symbol* s = (backend_symbol*)malloc(sizeof(backend_symbol));
			newsize = val - sym->val;
			s->name = strdup(name);
			s->val = val;
			s->type = type;
			s->size = sym->size - newsize;
			s->flags = flags;
			s->section = sym->section;
			s->src = strdup(sym->src);
			ll_insert(obj->symbol_table, iter, s);
			sym->size = newsize;
			return s;
		}
	}
	return NULL;
}

backend_symbol* backend_get_symbol_by_type_first(backend_object* obj, backend_symbol_type type)
{
   if (!obj || !obj->symbol_table)
      return NULL;

   for (obj->iter_symbol_t=ll_iter_start(obj->symbol_table); obj->iter_symbol_t != NULL; obj->iter_symbol_t=obj->iter_symbol_t->next)
	{
		backend_symbol* bs = (backend_symbol*)obj->iter_symbol_t->val;
		if (bs->type == type)
			return bs;
	}

	return NULL;
}

backend_symbol* backend_get_symbol_by_type_next(backend_object* obj, backend_symbol_type type)
{
   if (!obj || !obj->symbol_table)
      return NULL;

	obj->iter_symbol_t=obj->iter_symbol_t->next;

   for (; obj->iter_symbol_t != NULL; obj->iter_symbol_t=obj->iter_symbol_t->next)
	{
		backend_symbol* bs = (backend_symbol*)obj->iter_symbol_t->val;
		if (bs->type == type)
			return bs;
	}

	return NULL;
}
unsigned int backend_get_symbol_index(backend_object* obj, backend_symbol* s)
{
	unsigned int count = 0;

   if (!obj || !obj->symbol_table || !s)
      return (unsigned int)-1;

	//printf("+ %s\n", s->name);
   for (const list_node* iter=ll_iter_start(obj->symbol_table); iter != NULL; iter=iter->next)
	{
		//printf("** %s\n", ((backend_symbol*)(iter->val))->name);
		if (iter->val == s)
			return count;
		count++;
	}

	return (unsigned int)-1;
}

int backend_remove_symbol_by_name(backend_object* obj, const char* name)
{
	backend_symbol* bs;

   if (!obj || !obj->symbol_table)
      return -1;

	bs = (backend_symbol*)ll_remove(obj->symbol_table, name, cmp_by_name);
	if (bs)
	{
		free(bs->name);
		free(bs->src);
		free(bs);
		return 0;
	}

	return -2;
}

int backend_sort_symbols(backend_object* obj, backend_cmpfunc cmp)
{
	if (!obj || !obj->symbol_table)
		return -1;

	// create an empty list to hold the sorted elements
	linked_list *new_table = ll_init();

	// iterate over the original elements
   for (const list_node* iter=ll_iter_start(obj->symbol_table); iter != NULL; iter=iter->next)
	{
		backend_symbol *bs = (backend_symbol*)iter->val;

		// find its insertion point in the new list
		list_node *ii;
		list_node* i=0; // we will insert after 'i'

		for (ii = new_table->head; ii != NULL; i=ii,ii=ii->next)
		{
			backend_symbol *ss = (backend_symbol*)ii->val;
			if (cmp(bs, ss) < 0)
			{
				if (!i)
					ll_push(new_table, bs);
				else
					ll_insert(new_table, i, bs);
				break;
			}
		}
		if (ii == NULL)
			ll_add(new_table, bs);
	}

	// save the old one temporarily
	linked_list * lltmp = obj->symbol_table;

	// set the new list in its place
	obj->symbol_table = new_table;

	// delete the old list
	ll_destroy(lltmp);

	return 0;
}

void backend_set_source_file(backend_symbol *s, const char *filename)
{
	if (!s || !filename)
		return;

	if (s->src)
		free(s->src);

	s->src = strdup(filename);
}

///////////////////////////////////////////
unsigned int backend_section_count(backend_object* obj)
{
   if (!obj->section_table)
   {
      //printf("No section table yet\n");
      return 0;
   }
   return ll_size(obj->section_table);
}

backend_section* backend_add_section(backend_object* obj, const char* name, unsigned long size, unsigned long address, unsigned char* data, unsigned int entry_size, unsigned int alignment, unsigned long flags)
{
	if (!obj)
		return NULL;

   if (!obj->section_table)
      obj->section_table = ll_init();

   backend_section* s = (backend_section*)malloc(sizeof(backend_section));
	if (!s)
		return NULL;

	memset(s, 0, sizeof(backend_section));
   s->name = strdup(name);
   s->size = size;
   s->address = address;
   s->flags = flags;
	s->entry_size = entry_size;
   s->data = data;
	s->alignment = alignment;
   //printf("Adding section %s size:%i address:0x%lx entry size: %i flags:0x%x alignment %i\n", s->name, s->size, s->address, s->entry_size, s->flags, s->alignment);
   ll_add(obj->section_table, s);
   //printf("There are %i sections\n", backend_section_count(obj));
   return s;
}

void backend_section_set_type(backend_section *s, backend_section_type t)
{
	if (s)
		s->type = t;
}

void backend_section_set_index(backend_section *s, unsigned int i)
{
	s->index = i;
}

void backend_section_set_strtab(backend_section *s, backend_section* strtab)
{
	s->strtab = strtab;
}

backend_section* backend_get_section_by_index(backend_object* obj, unsigned int index)
{
	int i=1;
   for (const list_node* iter=ll_iter_start(obj->section_table); iter != NULL; iter=iter->next)
   {
      backend_section* sec = (backend_section*)iter->val;
		//printf("++ %i %s\n", sec->index, sec->name);
      if (i++ == index)
         return sec;
   }
   return NULL;
}

backend_section* backend_find_section_by_val(backend_object* obj, unsigned long val)
{
   for (const list_node* iter=ll_iter_start(obj->section_table); iter != NULL; iter=iter->next)
   {
      backend_section* sec = (backend_section*)iter->val;
      if (sec->address <= val && sec->address + sec->size > val)
         return sec;
   }
	return NULL;
}

backend_section* backend_get_section_by_name(backend_object* obj, const char* name)
{
	if (!obj || !name || !obj->section_table)
		return NULL;

   for (const list_node* iter=ll_iter_start(obj->section_table); iter != NULL; iter=iter->next)
   {
      backend_section* sec = (backend_section*)iter->val;
      if (!strcmp(name, sec->name))
         return sec;
   }
   return NULL;
}

backend_section* backend_get_section_by_type(backend_object* obj, unsigned int t)
{
	if (!obj || !obj->section_table)
		return NULL;

   for (const list_node* iter=ll_iter_start(obj->section_table); iter != NULL; iter=iter->next)
   {
      backend_section* sec = (backend_section*)iter->val;
      if (t == sec->type)
         return sec;
   }
   return NULL;
}

backend_section* backend_get_section_by_address(backend_object* obj, unsigned long address)
{
	if (!obj || !obj->section_table)
		return NULL;

   for (const list_node* iter=ll_iter_start(obj->section_table); iter != NULL; iter=iter->next)
   {
      backend_section* sec = (backend_section*)iter->val;
      if (address == sec->address)
         return sec;
   }
   return NULL;
}

int backend_get_section_index_by_name(backend_object* obj, const char* name)
{
	int index = 0;

	if (!obj || !name || !obj->section_table)
		return -1;

   for (const list_node* iter=ll_iter_start(obj->section_table); iter != NULL; iter=iter->next)
   {
      backend_section* sec = (backend_section*)iter->val;
		//printf("-- %s\n", sec->name);
      if (!strcmp(name, sec->name))
         return index+1;
		index++;
   }
   return -1;
}

backend_section* backend_get_first_section(backend_object* obj)
{
   if (!obj->section_table)
      return NULL;
   obj->iter_section = ll_iter_start(obj->section_table);
   return (backend_section*)obj->iter_section->val;
}

backend_section* backend_get_next_section(backend_object* obj)
{
   obj->iter_section = obj->iter_section->next; 
   if (obj->iter_section)
      return (backend_section*)obj->iter_section->val;
   return NULL;
}

backend_section* backend_get_first_section_by_type(backend_object* obj, backend_section_type t)
{
   if (!obj->section_table)
      return NULL;
   obj->iter_section = ll_iter_start(obj->section_table);
	while (obj->iter_section && ((backend_section*)obj->iter_section->val)->type != t)
		obj->iter_section = obj->iter_section->next;

   if (obj->iter_section)
		return (backend_section*)obj->iter_section->val;
   return NULL;
}

backend_section* backend_get_next_section_by_type(backend_object* obj, backend_section_type t)
{
	while (obj->iter_section && ((backend_section*)obj->iter_section->val)->type != t)
		obj->iter_section = obj->iter_section->next;

   if (obj->iter_section)
		return (backend_section*)obj->iter_section->val;
   return NULL;
}

backend_symbol* backend_get_section_symbol(backend_object* obj, backend_section* sec)
{
	backend_symbol* bs = backend_get_symbol_by_type_first(obj, SYMBOL_TYPE_SECTION);
	while (bs)
	{
		// I hate comparing pointers to objects like this, but what are my options?
		if (bs->section == sec)
		{
			//printf("Found symbol %s for section %s\n", bs->name, sec->name);
			break;
		}
		bs = backend_get_symbol_by_type_next(obj, SYMBOL_TYPE_SECTION);
	}
	return bs;
}

void backend_destructor(backend_object* obj)
{
	//printf("backend_destructor\n");
   // destroy the symbol table
   if (obj->symbol_table)
   {
      backend_symbol* s = (backend_symbol*)ll_pop(obj->symbol_table);
      while (s)
      {
         //printf("Popped %s\n", s->name);
         free(s->name);
         free(s->src);
         free(s);
         s = (backend_symbol*)ll_pop(obj->symbol_table);
      }
		free(obj->symbol_table);
   }

	if (obj->section_table)
   {
      backend_section* sec = (backend_section*)ll_pop(obj->section_table);
		while (sec)
		{
         free(sec->name);
         free(sec->data);
			free(sec);
			sec = (backend_section*)ll_pop(obj->section_table);
		}
		free(obj->section_table);
   }

   if (obj->relocation_table)
   {
		backend_reloc* r = (backend_reloc*)ll_pop(obj->relocation_table);
		while (r)
		{
			free(r);
			r = (backend_reloc*)ll_pop(obj->relocation_table);
		}
		free(obj->relocation_table);
	}

	if (obj->import_table)
	{
		backend_import* i = (backend_import*)ll_pop(obj->import_table);
		while (i)
		{
			if (i->symbols)
			{
				backend_symbol* s = (backend_symbol*)ll_pop(i->symbols);
				while (s)
				{
					free(s->name);
					free(s);
					s = (backend_symbol*)ll_pop(i->symbols);
				}
				free(i->symbols);
			}
			i = (backend_import*)ll_pop(obj->import_table);
		}
		free(obj->import_table);
	}

   // and finally the object itself
   free(obj);
}

///////////////////////////////////////////
unsigned int backend_relocation_count(backend_object* obj)
{
   if (obj && obj->relocation_table)
		return ll_size(obj->relocation_table);
   else
		return 0;
}

int backend_add_relocation(backend_object* obj, unsigned long offset, backend_reloc_type t, long addend, backend_symbol* bs)
{
   if (!obj)
		return -1;

	//printf("add relocation for %s @ 0x%lx type=%s\n", bs->name, offset, backend_lookup_reloc_type(t));
   if (!obj->relocation_table)
      obj->relocation_table = ll_init();

   backend_reloc* r = (backend_reloc*)malloc(sizeof(backend_reloc));
	r->offset = offset;
	r->addend = addend;
   r->type = t;
	r->symbol = bs;
   ll_add(obj->relocation_table, r);
   return 0;
}

backend_reloc* backend_find_reloc_by_offset(backend_object* obj, unsigned long offset)
{
	if (!obj || !obj->relocation_table)
		return NULL;

   for (const list_node* iter=ll_iter_start(obj->relocation_table); iter != NULL; iter=iter->next)
   {
      backend_reloc* rel = (backend_reloc*)iter->val;
		if (rel->offset == offset)
			return rel;
	}

	return NULL;
}

backend_reloc* backend_get_first_reloc(backend_object* obj)
{
	if (!obj || !obj->relocation_table)
		return NULL;
	obj->iter_reloc = ll_iter_start(obj->relocation_table);
	return (backend_reloc*)obj->iter_reloc->val;
}

backend_reloc* backend_get_next_reloc(backend_object* obj)
{
   obj->iter_reloc = obj->iter_reloc->next; 
   if (obj->iter_reloc)
      return (backend_reloc*)obj->iter_reloc->val;
   return NULL;
}

const char* backend_lookup_reloc_type(backend_reloc_type t)
{
	switch (t)
	{
	case RELOC_TYPE_NONE:
		return "none";
	case RELOC_TYPE_OFFSET:
		return "offset";
	case RELOC_TYPE_PC_RELATIVE:
		return "pc relative";
	case RELOC_TYPE_PLT:
		return "PLT relative";
	}

	return "unknown";
}

///////////////////////////////////////////
backend_import* backend_add_import_module(backend_object* obj, const char* name)
{
   if (!obj)
		return NULL;

   if (!obj->import_table)
      obj->import_table = ll_init();

   backend_import* i = (backend_import*)malloc(sizeof(backend_import));
	i->name = strdup(name);
	i->symbols = NULL;
   ll_add(obj->import_table, i);
   return i;
}

backend_import* backend_find_import_module_by_name(backend_object* obj, const char* name)
{
   if (!obj || !obj->import_table)
		return NULL;

   for (const list_node* iter=ll_iter_start(obj->import_table); iter != NULL; iter=iter->next)
   {
      backend_import* i = (backend_import*)iter->val;
		if (strcmp(i->name, name) == 0)
			return i;
	}

	return NULL;
}

backend_symbol* backend_add_import_function(backend_import* mod, const char* name, unsigned long addr)
{
   if (!mod)
		return NULL;

   if (!mod->symbols)
      mod->symbols = ll_init();

   backend_symbol* s = (backend_symbol*)malloc(sizeof(backend_symbol));
	s->name = strdup(name);
	s->val = addr;
	s->type = SYMBOL_TYPE_FUNCTION;
	s->flags = SYMBOL_FLAG_GLOBAL | SYMBOL_FLAG_EXTERNAL;
	s->size = 0;
	s->section = NULL;
   ll_add(mod->symbols, s);
   return s;
}

backend_symbol* backend_find_import_by_address(backend_object* obj, unsigned long addr)
{
   if (!obj || !obj->import_table)
		return NULL;

   for (const list_node* iter=ll_iter_start(obj->import_table); iter != NULL; iter=iter->next)
   {
      backend_import* i = (backend_import*)iter->val;
		if (i)
		{
   		for (const list_node* s_iter=ll_iter_start(i->symbols); s_iter != NULL; s_iter=s_iter->next)
			{
				backend_symbol* s = (backend_symbol*)s_iter->val;
				//printf("++ %s (0x%lx)\n", s->name, s->val);
				if (s && s->val == addr)
					return s;
			}
		}
	}

	return NULL;
}

backend_symbol* backend_get_first_import(backend_object* obj)
{
	if (!obj || !obj->import_table)
		return NULL;
	obj->iter_import_table = ll_iter_start(obj->import_table);
	if (!obj->iter_import_table)
		return NULL;

	backend_import* i = (backend_import*)obj->iter_import_table->val;
	if (!i)
		return NULL;

	obj->iter_import_symbol=ll_iter_start(i->symbols);
	if (!obj->iter_import_symbol)
		return NULL;
	return (backend_symbol*)obj->iter_import_symbol->val;
}

backend_symbol* backend_get_next_import(backend_object* obj)
{
	if (!obj || !obj->import_table)
		return NULL;

	// first, iterate inside the module
   obj->iter_import_symbol = obj->iter_import_symbol->next;
   if (obj->iter_import_symbol)
      return (backend_symbol*)obj->iter_import_symbol->val;

	// if there are no more symbols, try the next module
	obj->iter_import_table = obj->iter_import_table->next;
	if (!obj->iter_import_table)
		return NULL;

	backend_import* i = (backend_import*)obj->iter_import_table->val;
	if (!i)
		return NULL;

	obj->iter_import_symbol=ll_iter_start(i->symbols);
	if (!obj->iter_import_symbol)
		return NULL;
	return (backend_symbol*)obj->iter_import_symbol->val;
}

unsigned int backend_import_symbol_count(backend_object* obj)
{
	unsigned int count = 0;
	backend_symbol *s = backend_get_first_import(obj);
	while (s)
	{
		count++;
		s = backend_get_next_import(obj);
	}
	return count;
}
