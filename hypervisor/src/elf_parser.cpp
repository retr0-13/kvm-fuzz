#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <limits>
#include "elf_parser.h"

using namespace std;

ElfParser::ElfParser(const string& elf_path)
	: m_base(0)
	, m_path(elf_path)
{
	const char* cpath = m_path.c_str();

	// Load file into memory
	struct stat st;
	int fd = open(cpath, O_RDONLY);
	ERROR_ON(fd < 0, "elf %s: open", cpath);
	ERROR_ON(fstat(fd, &st) < 0, "elf %s: fstat", cpath);

	m_data = (uint8_t*)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	ERROR_ON(m_data == MAP_FAILED, "elf %s: mmap", cpath);
	close(fd);

	Elf_Ehdr* ehdr = (Elf_Ehdr*)m_data;
	Elf_Phdr* phdr = (Elf_Phdr*)(m_data + ehdr->e_phoff);
	Elf_Shdr* shdr = (Elf_Shdr*)(m_data + ehdr->e_shoff);
	m_phinfo = {
		.e_phoff     = ehdr->e_phoff,
		.e_phentsize = ehdr->e_phentsize,
		.e_phnum     = ehdr->e_phnum
	};
	m_type  = ehdr->e_type;
	m_entry = ehdr->e_entry;

	// Some checks
	ASSERT(ehdr->e_ident[EI_CLASS] == ELFCLASS,
	       "elf %s: BITS don't match (expecting %d)", cpath, BITS);
	ASSERT(ehdr->e_machine == EM,
	       "elf %s: MACH doesn't match (expecting %s)", cpath, EM_S);
	ASSERT(ehdr->e_type == ET_EXEC || ehdr->e_type == ET_DYN,
	       "elf %s: TYPE doesn't match (expecting executable or shared", cpath);

	// Get segments
	m_load_addr = numeric_limits<vaddr_t>::max();
	m_initial_brk = 0;
	size_t i;
	for (i = 0; i < ehdr->e_phnum; i++) {
		segment_t segment = {
			.type     = phdr[i].p_type,
			.flags    = phdr[i].p_flags,
			.offset   = phdr[i].p_offset,
			.vaddr    = phdr[i].p_vaddr,
			.paddr    = phdr[i].p_paddr,
			.filesize = phdr[i].p_filesz,
			.memsize  = phdr[i].p_memsz,
			.align    = phdr[i].p_align,
			.data     = m_data + segment.offset
		};
		m_segments.push_back(segment);

		// Update brk beyond any loadable segment, and load_addr as the address
		// of the first segment in memory
		if (segment.type == PT_LOAD) {
			m_load_addr = min(m_load_addr, segment.vaddr);
			m_initial_brk = max(m_initial_brk,
			                    (segment.vaddr + segment.memsize + 0xFFF) & ~0xFFF);
		}
		if (segment.type == PT_INTERP)
			m_interpreter = string((char*)segment.data);
	}

	// Get sections
	Elf_Shdr* sh_strtab = &shdr[ehdr->e_shstrndx];
	const char* strtab = (char*)m_data + sh_strtab->sh_offset;
	for (i = 0; i < ehdr->e_shnum; i++) {
		section_t section = {
			.name      = string(strtab + shdr[i].sh_name),
			.type      = shdr[i].sh_type,
			.flags     = shdr[i].sh_flags,
			.addr      = shdr[i].sh_addr,
			.offset    = shdr[i].sh_offset,
			.size      = shdr[i].sh_size,
			.link      = shdr[i].sh_link,
			.info      = shdr[i].sh_info,
			.addralign = shdr[i].sh_addralign,
			.entsize   = shdr[i].sh_entsize,
			.data      = m_data + section.offset
		};
		m_sections.push_back(section);
	}

	// Get symbols
	for (const section_t& section : m_sections) {
		// Symbols are defined in these two sections
		if (section.type != SHT_SYMTAB && section.type != SHT_DYNSYM)
			continue;

		Elf_Sym* syms = (Elf_Sym*)section.data;
		size_t n_syms = section.size / sizeof(Elf_Sym);

		// The string table could be sections .strtab or .dynstr. The index of
		// the string table section is specified in the symbol section link
		const char* sec_strtab = (char*)m_sections[section.link].data;
		for (i = 0; i < n_syms; i++) {
			symbol_t symbol = {
				.name       = string(sec_strtab + syms[i].st_name),
				.type       = ELF_ST_TYPE(syms[i].st_info),
				.binding    = ELF_ST_BIND(syms[i].st_info),
				.visibility = ELF_ST_VISIBILITY(syms[i].st_other),
				.shndx      = syms[i].st_shndx,
				.value      = syms[i].st_value,
				.size       = syms[i].st_size
			};
			m_symbols.push_back(symbol);
		}
	}

}

const uint8_t* ElfParser::data() const {
	return m_data;
}

void ElfParser::set_base(vaddr_t base) {
	vaddr_t diff = base - m_base;
	m_base = base;

	// Update all virtual addresses accordingly
	m_entry     += diff;
	m_load_addr += diff;
	for (segment_t& segment : m_segments) {
		segment.vaddr += diff;
		segment.paddr += diff;
	}
	for (section_t& section : m_sections) {
		section.addr += diff;
	}
	for (symbol_t& symbol : m_symbols) {
		symbol.value += diff;
	}
}

vaddr_t ElfParser::base() const {
	return m_base;
}

vaddr_t ElfParser::initial_brk() const {
	return m_initial_brk;
}

phinfo_t ElfParser::phinfo() const {
	return m_phinfo;
}

uint16_t ElfParser::type() const {
	return m_type;
}

vaddr_t ElfParser::entry() const {
	return m_entry;
}

vaddr_t ElfParser::load_addr() const {
	return m_load_addr;
}

string ElfParser::path() const {
	return m_path;
}

string ElfParser::interpreter() const {
	return m_interpreter;
}

vector<segment_t> ElfParser::segments() const {
	return m_segments;
}

vector<section_t> ElfParser::sections() const {
	return m_sections;
}

vector<symbol_t> ElfParser::symbols() const {
	return m_symbols;
}

/* vector<relocation_t> ElfParser::relocations() const {
	return m_relocations;
} */