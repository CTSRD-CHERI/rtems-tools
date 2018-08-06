/*
 * Copyright (c) 2018, Chris Johns <chrisj@rtems.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/**
 * @file
 *
 * @ingroup rtems-ld
 *
 * @brief RTEMS Linker DWARF module manages the DWARF format images.
 *
 */

#include <string.h>

#include <iostream>
#include <iomanip>
#include <list>
#include <map>

#include <rld.h>
#include <rld-path.h>
#include <rld-dwarf.h>

namespace rld
{
  namespace dwarf
  {
    typedef std::vector < dwarf_die > dies_active;
    dies_active active_dies;

    bool active_dies_present (dwarf_die die)
    {
      return std::find (active_dies.begin(), active_dies.end(), die) != active_dies.end();
    }

    void dies_active_add (dwarf_die die)
    {
      if (active_dies_present (die))
      {
        std::cout << "DDdd : dup : " << die << std::endl;
      }
      else
      {
        active_dies.push_back (die);
      }
    }

    void dies_active_remove (dwarf_die die)
    {
      dies_active::iterator di = std::find (active_dies.begin(), active_dies.end(), die);
      if (di == active_dies.end ())
      {
        std::cout << "DDdd : no found : " << die << std::endl;
      }
      else
      {
        active_dies.erase (di);
      }
    }

    /**
     * The libdwarf error.
     */
    void libdwarf_error_check (const std::string where,
                               int               result,
                               dwarf_error&      error)
    {
      if (result != DW_DLV_OK)
      {
        std::ostringstream exe_where;
        std::string        what;
        exe_where << "dwarf:" << where;
        what = dwarf_errmsg (error);
        throw rld::error (what, exe_where.str ());
      }
    }

    address::address (const sources& source, dwarf_line& line)
      : addr (0),
        source (&source),
        source_index (-1),
        source_line (-1),
        begin_statement (false),
        block (false),
        end_sequence (false)
    {
      dwarf_error de;
      int         dr;
      dr = ::dwarf_lineaddr(line, &addr, &de);
      libdwarf_error_check ("address::address", dr, de);
      dr = ::dwarf_line_srcfileno(line, &source_index, &de);
      libdwarf_error_check ("address::address", dr, de);
      dwarf_unsigned src_line;
      dr = ::dwarf_lineno(line, &src_line, &de);
      libdwarf_error_check ("address::address", dr, de);
      source_line = src_line;
      dwarf_bool b;
      dr = ::dwarf_linebeginstatement(line, &b, &de);
      libdwarf_error_check ("address::address", dr, de);
      begin_statement = b ? true : false;
      dr = ::dwarf_lineblock(line, &b, &de);
      libdwarf_error_check ("address::address", dr, de);
      block = b ? true : false;
      dr = ::dwarf_lineendsequence(line, &b, &de);
      libdwarf_error_check ("address::address", dr, de);
      end_sequence = b ? true : false;
    }

    address::address (const address& orig, const sources& source)
      : addr (orig.addr),
        source (&source),
        source_index (orig.source_index),
        source_line (orig.source_line),
        begin_statement (orig.begin_statement),
        block (orig.block),
        end_sequence (orig.end_sequence)
    {
    }

    address::address (const address& orig, dwarf_address addr)
      : addr (addr),
        source (orig.source),
        source_index (orig.source_index),
        source_line (orig.source_line),
        begin_statement (orig.begin_statement),
        block (orig.block),
        end_sequence (orig.end_sequence)
    {
    }

    address::address (const address& orig)
      : addr (orig.addr),
        source (orig.source),
        source_index (orig.source_index),
        source_line (orig.source_line),
        begin_statement (orig.begin_statement),
        block (orig.block),
        end_sequence (orig.end_sequence)
    {
    }

    address::address ()
      : addr (0),
        source (nullptr),
        source_index (-1),
        source_line (-1),
        begin_statement (false),
        block (false),
        end_sequence (false)
    {
    }

    address::~address ()
    {
      source = nullptr;
      source_line = -1;
    }

    bool
    address::valid () const
    {
      return source != nullptr && source_line > 0;
    }

    dwarf_address
    address::location () const
    {
      return addr;
    }

    std::string
    address::path () const
    {
      if (source == nullptr)
        throw rld::error ("invalid source pointer", "dwarf:address:path");
      return (*source)[source_index];
    }

    int
    address::line () const
    {
      return source_line;
    }

    bool
    address::is_a_begin_statement () const
    {
      return begin_statement;
    }

    bool
    address::is_in_a_block () const
    {
      return block;
    }

    bool
    address::is_an_end_sequence () const
    {
      return end_sequence;
    }

    address&
    address::operator = (const address& rhs)
    {
      if (this != &rhs)
      {
        addr = rhs.addr;
        source = rhs.source;
        source_index = rhs.source_index;
        source_line = rhs.source_line;
        begin_statement = rhs.begin_statement;
        block = rhs.block;
        end_sequence = rhs.end_sequence;
      }
      return *this;
    }

    bool
    address::operator < (const address& rhs) const
    {
      return addr < rhs.addr;
    }

    range::range (const dwarf_ranges* range)
      : range_ (range)
    {
    }

    range::range (const range& orig)
      : range_ (orig.range_)
    {
    }

    range::~range ()
    {
    }

    dwarf_unsigned
    range::addr1 () const
    {
      if (range_ == nullptr)
        throw rld::error ("No valid range", "rld:dwarf:range:addr1");
      return range_->dwr_addr1;
    }

    dwarf_unsigned
    range::addr2 () const
    {
      if (range_ == nullptr)
        throw rld::error ("No valid range", "rld:dwarf:range:addr2");
      return range_->dwr_addr2;
    }

    dwarf_ranges_type
    range::type () const
    {
      if (range_ == nullptr)
        throw rld::error ("No valid range", "rld:dwarf:range:type");
      return range_->dwr_type;
    }

    bool
    range::empty () const
    {
      /**
       * See DWARF 2.17.3.
       *
       * A bounded range entry whose beginning and ending address offsets are
       * equal (including zero) indicates an empty range and may be ignored.
       */
      return type () == DW_RANGES_ENTRY && addr1 () == addr2 ();
    }

    bool
    range::end () const
    {
      return type () == DW_RANGES_END;
    }

    range&
    range::operator = (const range& rhs)
    {
      if (this != &rhs)
        range_ = rhs.range_;
      return *this;
    }

    void
    range::dump ()
    {
      dwarf_ranges_type type_ = type ();
      const char*       type_s = "invalid";
      const char*       type_labels[] = {
        "BOUNDED",
        "BASE",
        "END"
      };
      if (type_ <= DW_RANGES_END)
        type_s = type_labels[type_];
      std::cout << type_s << '-'
                << std::hex << std::setfill ('0')
                << "0x" << std::setw (8) << addr1 ()
                << ":0x" << std::setw (8) << addr2 ();
    }

    address_ranges::address_ranges (file& debug)
      : debug (debug),
        offset (-1),
        dranges (nullptr),
        dranges_count (0)
    {
    }

    address_ranges::address_ranges (debug_info_entry& die)
      : debug (die.get_debug ()),
        offset (-1),
        dranges (nullptr),
        dranges_count (0)
    {
      load (die);
    }

    address_ranges::address_ranges (file& debug, dwarf_offset offset)
      : debug (debug),
        offset (offset),
        dranges (nullptr),
        dranges_count (0)
    {
      load (offset);
    }

    address_ranges::address_ranges (const address_ranges& orig)
      : debug (orig.debug),
        offset (orig.offset)
    {
      load (orig.offset);
    }

    address_ranges::~address_ranges ()
    {
      if (dranges != nullptr)
      {
        ::dwarf_ranges_dealloc (debug, dranges, dranges_count);
        dranges = nullptr;
        dranges_count = 0;
        ranges_.clear ();
      }
    }

    bool
    address_ranges::load (debug_info_entry& die, bool error)
    {
      dwarf_attribute attr;
      dwarf_error     de;
      int             dr;
      dr = ::dwarf_attr (die, DW_AT_ranges, &attr, &de);
      if (dr != DW_DLV_OK)
      {
        if (!error)
          return false;
        libdwarf_error_check ("rld:dwarf::address_ranges:load", dr, de);
      }
      dr = ::dwarf_global_formref (attr, &offset, &de);
      if (dr != DW_DLV_OK)
      {
        if (!error)
          return false;
        libdwarf_error_check ("rld:dwarf::address_ranges:load", dr, de);
      }
      load (offset);
      return true;
    }

    bool
    address_ranges::load (dwarf_offset offset_, bool error)
    {
      if (offset_ > 0)
      {
        if (dranges != nullptr)
          ::dwarf_ranges_dealloc (debug, dranges, dranges_count);

        dranges = nullptr;
        dranges_count = 0;

        offset = offset_;

        dwarf_error de;
        int         dr;

        dr = ::dwarf_get_ranges (debug, offset,
                                 &dranges, &dranges_count, nullptr, &de);
        if (dr != DW_DLV_OK)
        {
          if (!error)
            return false;
          libdwarf_error_check ("rld:dwarf::ranges:load", dr, de);
        }

        if (dranges != nullptr && dranges_count > 0)
        {
          for (dwarf_signed r = 0; r < dranges_count; ++r)
            ranges_.push_back (range (&dranges[r]));
        }
      }

      return true;
    }

    const ranges&
    address_ranges::get () const
    {
      return ranges_;
    }

    bool
    address_ranges::empty () const
    {
      return ranges_.empty ();
    }

    address_ranges&
    address_ranges::operator = (const address_ranges& rhs)
    {
      if (this != &rhs)
      {
        if (debug != rhs.debug)
          throw rld::error ("invalid debug", "address_ranges:=");
        load (rhs.offset);
      }
      return *this;
    }

    void
    address_ranges::dump ()
    {
      bool first = true;
      std::cout << '[';
      for (auto& r : ranges_)
      {
        if (!first)
          std::cout << ',';
        r.dump ();
        first = false;
      }
      std::cout << ']';
    }

    line_addresses::line_addresses (file&             debug,
                                    debug_info_entry& die)
      : debug (debug),
        lines (nullptr),
        count_ (0)
    {
      if (!die.source_lines (lines, count_))
      {
        lines = nullptr;
        count_ = 0;
      }
    }

    line_addresses::~line_addresses ()
    {
      if (lines && count_ > 0)
      {
        ::dwarf_srclines_dealloc (debug, lines, count_);
        lines = nullptr;
        count_ = 0;
      }
    }

    size_t
    line_addresses::count () const
    {
      return count_;
    }

    dwarf_line&
    line_addresses::operator [] (const int index)
    {
      if (index < 0 || index >= count_)
        throw rld::error ("index out of range",
                          "line_addresses:indexing");
      return lines[index];
    }

    sources::sources (file& debug, dwarf_offset die_offset)
      : debug (debug),
        source (nullptr),
        count (0),
        die_offset (die_offset)
    {
      debug_info_entry die (debug, die_offset);
      die.source_files (source, count);
    }

    sources::sources (const sources& orig)
      : debug (orig.debug),
        source (nullptr),
        count (0),
        die_offset (orig.die_offset)
    {
      /*
       * In a copy constructor we need to get our own copy of the strings. To
       * do that we need to get the DIE at the offset in the original.
       */
      debug_info_entry die (debug, die_offset);
      die.source_files (source, count);
    }

    sources::~sources ()
    {
      dealloc ();
    }

    std::string
    sources::operator [] (const int index) const
    {
      if (index <= 0 || index > count)
        return "unknown";
      return std::string (source[index - 1]);
    }

    void
    sources::dealloc ()
    {
      if (source && count > 0)
      {
        /*
         * The elftoolchain cleans the memory up and there is no compatible
         * call we can put here so adding the required code causes is a double
         * free resulting in a crash.
         */
        if (false)
        {
          for (int s = 0; s < count; ++s)
            ::dwarf_dealloc (debug, source[s], DW_DLA_STRING);
          ::dwarf_dealloc (debug, source, DW_DLA_LIST);
        }
        source = nullptr;
        count = 0;
      }
    }

    sources&
    sources::operator = (sources&& rhs)
    {
      if (this != &rhs)
      {
        debug = rhs.debug;
        source = rhs.source;
        count = rhs.count;
        die_offset = rhs.die_offset;
        rhs.source = nullptr;
        rhs.count = 0;
      }
      return *this;
    }


    function::function (file& debug, debug_info_entry& die)
      : debug (debug),
        machine_code_ (false),
        external_ (false),
        declaration_ (false),
        inline_ (DW_INL_not_inlined),
        pc_low_ (0),
        pc_high_ (0),
        ranges_ (debug)
    {
      dwarf_bool db;

      if (die.attribute (DW_AT_external, db, false))
        external_ = db ? true : false;

      if (die.attribute (DW_AT_declaration, db, false))
        declaration_ = db ? true : false;

      die.attribute (DW_AT_linkage_name, linkage_name_, false);

      if (!die.attribute (DW_AT_inline, inline_, false))
        inline_ = DW_INL_not_inlined;

      if (inline_ == DW_INL_declared_inlined)
      {
        die_dump_children (die, " +");
      }

      /*
       * If ranges are not found see if the PC low and PC high attributes
       * can be found.
       */
      ranges_.load (die, false);
      if (ranges_.empty ())
      {
        bool is_address;
        if (die.get_lowpc (pc_low_) && die.get_highpc (pc_high_, is_address))
        {
          machine_code_ = true;
          if (!is_address)
            pc_high_ += pc_low_;
        }
      }
      else
      {
        for (auto& r : ranges_.get ())
        {
          if (!r.end () && !r.empty ())
          {
            machine_code_ = true;
            break;
          }
        }
      }

      if (declaration_)
      {
        die.attribute (DW_AT_name, name_);
      }
      else
      {
        /*
         * Get the name attribute. (if present)
         */
        if (!die.attribute (DW_AT_name, name_, false))
        {
          bool found = false;

          /*
           * For inlined function, the actual name is probably in the DIE
           * referenced by DW_AT_abstract_origin. (if present)
           */
          dwarf_attribute abst_at;
          if (die.attribute (DW_AT_abstract_origin, abst_at, false))
          {
            dwarf_offset abst_at_die_offset;
            dwarf_error  de;
            int          dr;
            dr = ::dwarf_global_formref (abst_at, &abst_at_die_offset, &de);
            if (dr == DW_DLV_OK)
            {
              debug_info_entry abst_at_die (debug, abst_at_die_offset);
              if (abst_at_die.attribute (DW_AT_name, name_, false))
                found = true;
            }
          }

          /*
           * If DW_AT_name is not present, but DW_AT_specification is present,
           * then probably the actual name is in the DIE referenced by
           * DW_AT_specification.
           */
          if (!found)
          {
            dwarf_attribute spec;
            if (die.attribute (DW_AT_specification, spec, false))
            {
              dwarf_offset spec_die_offset;
              dwarf_error  de;
              int          dr;
              dr = ::dwarf_global_formref (spec, &spec_die_offset, &de);
              if (dr == DW_DLV_OK)
              {
                debug_info_entry spec_die (debug, spec_die_offset);
                if (spec_die.attribute (DW_AT_name, name_, false))
                  found = true;
              }
            }
          }
        }

        if (die.tag () == DW_TAG_inlined_subroutine)
        {
          die.attribute (DW_AT_call_file, call_file_, false);
        }
      }

      if (rld::verbose () >= RLD_VERBOSE_FULL_DEBUG)
      {
        std::cout << "dwarf::function: ";
        if (name_.empty ())
          std::cout << "NO NAME";
        else
          std::cout << name_;
        if (!has_machine_code ())
          std::cout << " NO MACHINE CODE";
        else
          std::cout << std::hex << std::setfill ('0')
                    << " pc_low = 0x" << std::setw (8) << pc_low ()
                    << " pc_high = 0x" << std::setw (8) << pc_high ()
                    << std::dec << std::setfill (' ');
        std::cout << std::endl;
      }
    }

    function::~function ()
    {
    }

    std::string
    function::name () const
    {
      return name_;
    }

    const address_ranges&
    function::get_ranges () const
    {
      return ranges_;
    }

    dwarf_unsigned
    function::pc_low () const
    {
      if (ranges_.empty ())
        return pc_low_;
      dwarf_address addr = ~0;
      for (auto& r : ranges_.get ())
      {
        if (!r.end () && !r.empty () && r.addr1 () < addr)
          addr = r.addr1 ();
      }
      return addr;
    }

    dwarf_unsigned
    function::pc_high () const
    {
      if (ranges_.empty ())
        return pc_high_;
      dwarf_address addr = 0;
      for (auto& r : ranges_.get ())
      {
        if (!r.end () && !r.empty () && r.addr2 () > addr)
          addr = r.addr1 ();
      }
      return addr;
    }

    bool
    function::has_machine_code () const
    {
      return machine_code_;
    }

    bool
    function::is_external () const
    {
      return external_;
    }

    bool
    function::is_declaration () const
    {
      return declaration_;
    }

    bool
    function::is_inlined () const
    {
      return inline_ == DW_INL_declared_inlined;
    }

    std::string
    function::call_file () const
    {
      return call_file_;
    }

    bool
    function::inside (dwarf_address addr) const
    {
      return !name_.empty () && has_machine_code () &&
        addr >= pc_low () && addr <= pc_high ();
    }

    debug_info_entry::debug_info_entry (file& debug)
      : debug (debug),
        die (nullptr),
        tag_ (0),
        offset_ (0)
    {
    }

    debug_info_entry::debug_info_entry (file& debug, dwarf_die& die)
      : debug (debug),
        die (die),
        tag_ (0),
        offset_ (0)
    {
    }

    debug_info_entry::debug_info_entry (file& debug, dwarf_offset offset__)
      : debug (debug),
        die (nullptr),
        tag_ (0),
        offset_ (offset__)
    {
      dwarf_die   ddie;
      dwarf_error de;
      int         dr;
      dr = ::dwarf_offdie (debug, offset_, &ddie, &de);
      libdwarf_error_check ("debug_info_entry:debug_info_entry", dr, de);
      die = ddie;
    }

    debug_info_entry::debug_info_entry (const debug_info_entry& orig)
      : debug (orig.debug),
        die (nullptr),
        tag_ (orig.tag_),
        offset_ (orig.offset_)
    {
      if (offset_ != 0)
      {
        dwarf_die   ddie;
        dwarf_error de;
        int         dr;
        dr = ::dwarf_offdie (debug, offset_, &ddie, &de);
        libdwarf_error_check ("debug_info_entry:debug_info_entry", dr, de);
        die = ddie;
      }
    }

    debug_info_entry::~debug_info_entry ()
    {
      dealloc ();
    }

    dwarf_die
    debug_info_entry::get () const
    {
      return die;
    }

    debug_info_entry::operator dwarf_die& ()
    {
      return die;
    }

    debug_info_entry::operator dwarf_die* ()
    {
      return &die;
    }

    debug_info_entry&
    debug_info_entry::operator = (debug_info_entry& rhs)
    {
      if (this != &rhs)
      {
        if (debug != rhs.debug)
          throw rld::error ("DIE debug info mismatch",
                            "dwarf:debug_info_entry:operator=");
        dealloc ();
        die = rhs.die;
        tag_ = rhs.tag_;
        offset_ = rhs.offset_;
        rhs.die = nullptr;
      }
      return *this;
    }

    debug_info_entry&
    debug_info_entry::operator = (dwarf_offset offset__)
    {
      dealloc ();
      if (offset__ != 0)
      {
        dwarf_die   ddie;
        dwarf_error de;
        int         dr;
        offset_ = offset__;
        tag_ = 0;
        dr = ::dwarf_offdie (debug, offset_, &ddie, &de);
        libdwarf_error_check ("debug_info_entry:operator=", dr, de);
        die = ddie;
      }
      return *this;
    }

    bool
    debug_info_entry::operator == (debug_info_entry& rhs) const
    {
      return debug == rhs.debug && die == rhs.die &&
        tag_ == rhs.tag_ && offset_ == rhs.offset_;
    }

    bool
    debug_info_entry::operator == (const dwarf_die rhs) const
    {
      return die == rhs;
    }

    dwarf_tag
    debug_info_entry::tag ()
    {
      if (tag_ == 0)
      {
        dwarf_error de;
        int         dr;
        dr = ::dwarf_tag (die, &tag_, &de);
        libdwarf_error_check ("debug_info_entry:debug_info_entry", dr, de);
      }
      return tag_;
    }

    dwarf_offset
    debug_info_entry::offset ()
    {
      if (offset_ == 0)
      {
        dwarf_error de;
        int         dr;
        dr = ::dwarf_dieoffset (die, &offset_, &de);
        libdwarf_error_check ("debug_info_entry:debug_info_entry", dr, de);
      }
      return offset_;
    }

    bool
    debug_info_entry::get_lowpc (dwarf_address& addr, bool error) const
    {
      dwarf_error de;
      int         dr;
      dr = ::dwarf_lowpc (die, &addr, &de);
      if (error)
        libdwarf_error_check ("debug_info_entry:lowpc", dr, de);
      return dr == DW_DLV_OK;
    }

    bool
    debug_info_entry::get_highpc (dwarf_address& addr,
                                  bool&          is_address,
                                  bool           error) const
    {
      dwarf_half       form;
      dwarf_form_class class_;
      dwarf_error      de;
      int              dr;
      dr = ::dwarf_highpc_b (die, &addr, &form, &class_, &de);
      if (error)
        libdwarf_error_check ("debug_info_entry:highpc", dr, de);
      is_address = class_ == DW_FORM_CLASS_ADDRESS;
      return dr == DW_DLV_OK;
    }

    bool
    debug_info_entry::attribute (dwarf_attr       attr,
                                 dwarf_attribute& value,
                                 bool             error) const
    {
      dwarf_error de;
      int         dr;
      dr = ::dwarf_attr (die, attr, &value, &de);
      if (error)
        libdwarf_error_check ("debug_info_entry:attribute(attr)", dr, de);
      return dr == DW_DLV_OK;
    }

    bool
    debug_info_entry::attribute (dwarf_attr  attr,
                                 dwarf_bool& value,
                                 bool        error) const
    {
      dwarf_error de;
      int         dr;
      dr = ::dwarf_attrval_flag (die, attr, &value, &de);
      if (error)
        libdwarf_error_check ("debug_info_entry:attribute(flag)", dr, de);
      return dr == DW_DLV_OK;
    }

    bool
    debug_info_entry::attribute (dwarf_attr      attr,
                                 dwarf_unsigned& value,
                                 bool            error) const
    {
      dwarf_error de;
      int         dr;
      dr = ::dwarf_attrval_unsigned (die, attr, &value, &de);
      if (error)
        libdwarf_error_check ("debug_info_entry:attribute(unsigned)", dr, de);
      return dr == DW_DLV_OK;
    }

    bool
    debug_info_entry::attribute (dwarf_attr   attr,
                                 std::string& value,
                                 bool         error) const
    {
      dwarf_error de;
      int         dr;
      const char* s = nullptr;
      value.clear ();
      dr = ::dwarf_attrval_string (die, attr, &s, &de);
      if (error)
        libdwarf_error_check ("debug_info_entry:attribute(string)", dr, de);
      if (s != nullptr)
        value = s;
      return dr == DW_DLV_OK;
    }

    bool
    debug_info_entry::source_lines (dwarf_line*&  lines,
                                    dwarf_signed& linecount) const
    {
      dwarf_error de;
      int         dr;
      if (lines != nullptr)
        throw rld::error ("lines is not null", "debug_info_entry:source_lines");
      linecount = 0;
      dr = ::dwarf_srclines (die, &lines, &linecount, &de);
      if (dr ==  DW_DLV_NO_ENTRY)
        return false;
      libdwarf_error_check ("debug_info_entry:source_lines ", dr, de);
      return true;
    }

    void
    debug_info_entry::source_files (char**&       sources,
                                    dwarf_signed& sourcecount) const
    {
      dwarf_error de;
      int         dr;
      dr = ::dwarf_srcfiles (die, &sources, &sourcecount, &de);
      libdwarf_error_check ("debug_info_entry:source_files ", dr, de);
    }

    bool
    debug_info_entry::ranges (dwarf_ranges*& ranges,
                              dwarf_signed&  rangescount) const
    {
      dwarf_unsigned ranges_off;
      if (attribute (DW_AT_ranges, ranges_off, false))
      {
        dwarf_error de;
        int         dr;
        dr = ::dwarf_get_ranges (debug, ranges_off,
                                 &ranges, &rangescount, nullptr, &de);
        libdwarf_error_check ("debug_info_entry:ranges ", dr, de);
        return ranges != nullptr && rangescount > 0;
      }
      return false;
    }

    bool
    debug_info_entry::get_child (debug_info_entry& child_die)
    {
      dwarf_error      de;
      int              dr;
      dr = ::dwarf_child (die, child_die, &de);
      return dr == DW_DLV_OK;
    }

    bool
    debug_info_entry::get_sibling (debug_info_entry& sibling_die)
    {
      dwarf_error      de;
      int              dr;
      dr = ::dwarf_siblingof (debug, die, sibling_die, &de);
      if (dr == DW_DLV_NO_ENTRY)
        return false;
      libdwarf_error_check ("compilation_unit::sibling", dr, de);
      return true;
    }

    file&
    debug_info_entry::get_debug ()
    {
      return debug;
    }

    void
    debug_info_entry::dealloc ()
    {
      if (die != nullptr) {
        ::dwarf_dealloc (debug, die, DW_DLA_DIE);
        die = nullptr;
      }
    }

    void
    debug_info_entry::dump (std::string prefix, bool newline)
    {
      const char* s;
      ::dwarf_get_TAG_name (tag (), &s);
      std::cout << prefix << s << std::endl;

      dwarf_attribute* attributes;
      dwarf_signed     attr_count;
      dwarf_error      de;
      int              dr;

      dr = ::dwarf_attrlist (die, &attributes, &attr_count, &de);
      if (dr == DW_DLV_OK)
      {
        for (int a = 0; a < attr_count; ++a)
        {
          dwarf_attr attr;
          dr = ::dwarf_whatattr (attributes[a], &attr, &de);
          libdwarf_error_check ("debug_info_entry::dump", dr, de);
          dwarf_half form;
          dr = ::dwarf_whatform (attributes[a], &form, &de);
          libdwarf_error_check ("debug_info_entry::dump", dr, de);
          const char* f;
          dwarf_get_FORM_name (form, &f);
          dwarf_get_AT_name (attr, &s);
          if (a > 0)
            std::cout << std::endl;
          std::cout << prefix << " - " << s << " (" << attr << ") [" << f << ']';
          debug_info_entry v_die (debug);
          address_ranges   v_ranges (debug);
          dwarf_unsigned   v_unsigned;
          dwarf_bool       v_bool;
          dwarf_offset     v_offset;
          switch (form)
          {
            case DW_FORM_block:
            case DW_FORM_block1:
            case DW_FORM_block2:
            case DW_FORM_block4:
              break;
            case DW_FORM_addr:
            case DW_FORM_data1:
            case DW_FORM_data2:
            case DW_FORM_data4:
            case DW_FORM_data8:
            case DW_FORM_udata:
              dr = ::dwarf_attrval_unsigned (die, attr, &v_unsigned, &de);
              libdwarf_error_check ("debug_info_entry::dump", dr, de);
              std::cout << " : "
                        << std::hex << std::setfill ('0')
                        << std::setw (8) << v_unsigned
                        << std::dec << std::setfill (' ')
                        << " (" << v_unsigned << ')';
              break;
            case DW_FORM_ref1:
            case DW_FORM_ref2:
            case DW_FORM_ref4:
            case DW_FORM_ref8:
            case DW_FORM_ref_udata:
              dr = ::dwarf_global_formref (attributes[a], &v_offset, &de);
              libdwarf_error_check ("debug_info_entry::dump", dr, de);
              std::cout << " : "
                        << std::hex << std::setfill ('0')
                        << std::setw (8) << v_offset
                        << std::dec << std::setfill (' ')
                        << " (" << v_offset << ')';
              switch (attr)
              {
                case DW_AT_abstract_origin:
                case DW_AT_specification:
                  v_die = v_offset;
                  std::cout << std::endl;
                  v_die.dump (' ' + prefix, false);
                  break;
                default:
                  break;
              }
              break;
            case DW_FORM_exprloc:
              break;
            case DW_FORM_flag:
            case DW_FORM_flag_present:
              dr = ::dwarf_attrval_flag (die, attr, &v_bool, &de);
              libdwarf_error_check ("debug_info_entry::dump", dr, de);
              std::cout << " : " << v_bool;
              break;
              break;
            case DW_FORM_string:
            case DW_FORM_strp:
              dr = ::dwarf_attrval_string (die, attr, &s, &de);
              libdwarf_error_check ("debug_info_entry::dump", dr, de);
              std::cout << " : " << s;
              break;
            case DW_FORM_sec_offset:
              switch (attr)
              {
                case DW_AT_ranges:
                  dr = ::dwarf_global_formref (attributes[a], &v_offset, &de);
                  libdwarf_error_check ("debug_info_entry::dump", dr, de);
                  std::cout << ' ';
                  v_ranges.load (v_offset);
                  v_ranges.dump ();
                  break;
                default:
                  break;
              }
              break;
            case DW_FORM_indirect:
            case DW_FORM_ref_addr:
            case DW_FORM_ref_sig8:
            case DW_FORM_sdata:
              break;
          }
        }
        if (newline)
          std::cout << std::endl;
      }
    }

    void
    die_dump_children (debug_info_entry die,
                       std::string      prefix,
                       int              nesting,
                       int              depth)
    {
      debug_info_entry child (die.get_debug ());
      if (die.get_child (child))
        die_dump (child, prefix, nesting, depth);
    }

    void
    die_dump (debug_info_entry die,
              std::string      prefix,
              int              nesting,
              int              depth)
    {
      ++nesting;

      for (int n = 0; n < nesting; ++n)
        prefix += ' ';

      while (true)
      {
        die.dump (prefix);

        if (depth < 0 || nesting < depth)
          die_dump_children (die, prefix);

        debug_info_entry next (die.get_debug ());

        if (!die.get_sibling (next))
          break;

        die = next;
      }
    }

    compilation_unit::compilation_unit (file&             debug,
                                        debug_info_entry& die,
                                        dwarf_unsigned    offset)
      : debug (debug),
        offset_ (offset),
        pc_low_ (0),
        pc_high_ (0),
        ranges_ (debug),
        die_offset (die.offset ()),
        source_ (debug, die_offset)
    {
      die.attribute (DW_AT_name, name_);
      name_ = name_;

      die.attribute (DW_AT_producer, producer_);

      ranges_.load (die, false);

      if (ranges_.empty ())
      {
        bool is_address;
        die.get_lowpc (pc_low_);
        if (die.get_highpc (pc_high_, is_address))
        {
          if (!is_address)
            pc_high_ += pc_low_;
        }
        else
          pc_high_ = ~0U;
      }
      else
      {
        pc_low_ = ~0U;
        for (auto& r : ranges_.get ())
        {
          if (!r.end () && !r.empty () && r.addr1 () < pc_low_)
            pc_low_ = r.addr1 ();
        }
        pc_high_ = 0U;
        for (auto& r : ranges_.get ())
        {
          if (!r.end () && !r.empty () && r.addr2 () > pc_high_)
            pc_high_ = r.addr2 ();
        }
      }

      if (rld::verbose () >= RLD_VERBOSE_FULL_DEBUG)
      {
        std::cout << std::hex << std::setfill ('0')
                  << "dwarf::compilation_unit: "
                  << rld::path::basename (name_)
                  << ": (0x" << std::setw (8) << offset_ << ") ";
        if (pc_low_ != 0 && pc_high_ != ~0U)
          std::cout << "pc_low = 0x" << std::setw (8) << pc_low_
                    << " pc_high = 0x" << std::setw (8) << pc_high_;
       std::cout  << std::setfill (' ') << std::dec
                  << std::endl
                  << " ] " << producer_
                  << std::endl;
      }

      line_addresses lines (debug, die);
      dwarf_address  pc = 0;
      bool           seq_check = true;
      dwarf_address  seq_base = 0;

      for (size_t l = 0; l < lines.count (); ++l)
      {
        address       daddr (source_, lines[l]);
        dwarf_address loc = daddr.location ();
        /*
         * A CU's line program can have some sequences at the start where the
         * address is incorrectly set to 0. Ignore these entries.
         */
        if (pc == 0)
        {
          if (!seq_check)
          {
            seq_check = daddr.is_an_end_sequence ();
            continue;
          }
          if (loc == 0)
          {
            seq_check = false;
            continue;
          }
        }
        /*
         * A sequence of line program instruction may set the address to 0. Use
         * the last location from the previous sequence as the sequence's base
         * address. All locations will be offset from the that base until the
         * end of this sequence.
         */
        if (loc == 0 && seq_base == 0)
          seq_base = pc;
        if (seq_base != 0)
          loc += seq_base;
        if (daddr.is_an_end_sequence ())
          seq_base = 0;
        address addr (daddr, loc);
        if (loc >= pc_low_ && loc < pc_high_)
        {
          pc = loc;
          addr_lines_.push_back (addr);
        }
      }

      if (!addr_lines_.empty ())
      {
        std::stable_sort (addr_lines_.begin (), addr_lines_.end ());
        if (rld::verbose () >= RLD_VERBOSE_FULL_DEBUG)
        {
          auto first = addr_lines_.begin ();
          auto last = addr_lines_.end () - 1;
          std::cout << "dwarf::compilation_unit: line_low=0x"
                    << std::hex
                    << std::setw (8) << first->location ()
                    << ", line_high=0x"
                    << std::setw (8) << last->location ()
                    << std::dec
                    << std::endl;
        }
      }

      if (rld::verbose () >= RLD_VERBOSE_FULL_DEBUG)
      {
        int lc = 0;
        for (auto& l : addr_lines_)
        {
          std::cout << "dwarf::compilation_unit: " << std::setw (3) << ++lc
                    << ": 0x"
                    << std::hex << std::setw (8) << l.location () << std::dec
                    << " - "
                    << (char) (l.is_a_begin_statement () ? 'B' : '.')
                    << (char) (l.is_in_a_block () ? 'I' : '.')
                    << (char) (l.is_an_end_sequence () ? 'E' : '.')
                    << " - "
                    << rld::path::basename (l.path ())
                    << ':' <<l.line ()
                    << std::endl;
        }
      }
    }

    compilation_unit::compilation_unit (const compilation_unit& orig)
      : debug (orig.debug),
        offset_ (orig.offset_),
        name_ (orig.name_),
        producer_ (orig.producer_),
        pc_low_ (orig.pc_low_),
        pc_high_ (orig.pc_high_),
        ranges_ (orig.ranges_),
        die_offset (orig.die_offset),
        source_ (debug, die_offset)
    {
      for (auto& line : orig.addr_lines_)
        addr_lines_.push_back (address (line, source_));
      std::stable_sort (addr_lines_.begin (), addr_lines_.end ());
    }

    compilation_unit::~compilation_unit ()
    {
    }

    void
    compilation_unit::load_types ()
    {
      //dump_die ();
    }

    void
    compilation_unit::load_functions ()
    {
      debug_info_entry die (debug, die_offset);
      debug_info_entry ret_die (debug);
      dwarf_error      de;
      int              dr;
      dr = ::dwarf_child(die, ret_die, &de);
      if (dr == DW_DLV_OK)
        load_functions (ret_die);
    }

    void
    compilation_unit::load_functions (debug_info_entry& die)
    {
      while (true)
      {
        if (die.tag () == DW_TAG_subprogram ||
            die.tag () == DW_TAG_entry_point ||
            die.tag () == DW_TAG_inlined_subroutine)
        {
          function func (debug, die);
          if (func.has_machine_code () &&
              func.pc_low () >= pc_low_ && func.pc_high () <= pc_high_)
          {
            functions_.push_back (func);
          }
        }

        debug_info_entry ret_die (debug);
        dwarf_error      de;
        int              dr;

        dr = ::dwarf_child(die, ret_die, &de);
        if (dr == DW_DLV_OK)
          load_functions (ret_die);

        dr = ::dwarf_siblingof (debug, die, ret_die, &de);
        if (dr == DW_DLV_NO_ENTRY)
          break;
        libdwarf_error_check ("compilation_unit:load_functions", dr, de);

        die = ret_die;
      }
    }

    std::string
    compilation_unit::name () const
    {
      return name_;
    }

    std::string
    compilation_unit::producer () const
    {
      return producer_;
    }

    unsigned int
    compilation_unit::pc_low () const
    {
      return pc_low_;
    }

    unsigned int
    compilation_unit::pc_high () const
    {
      return pc_high_;
    }

    bool
    compilation_unit::get_source (const dwarf_address addr,
                                  address&            addr_line)
    {
      if (!addr_lines_.empty () && inside (addr))
      {
        address last_loc;
        for (auto& loc : addr_lines_)
        {
          if (addr <= loc.location ())
          {
            if (addr == loc.location ())
              addr_line = loc;
            else
              addr_line = last_loc;
            return addr_line.valid ();
          }
          last_loc = loc;
        }
      }
      return false;
    }

    functions&
    compilation_unit::get_functions ()
    {
      return functions_;
    }

    bool
    compilation_unit::inside (dwarf_unsigned addr) const
    {
      return addr >= pc_low_ && addr < pc_high_;
    }

    compilation_unit&
    compilation_unit::operator = (const compilation_unit& rhs)
    {
      if (this != &rhs)
      {
        debug = rhs.debug;

        /*
         * This is a copy operator so we need to get a new copy of the strings,
         * we cannot steal the other copy.
         */
        offset_ = rhs.offset_;
        name_ = rhs.name_;
        producer_ = rhs.producer_;
        source_ = sources (debug, die_offset);
        for (auto& line : rhs.addr_lines_)
          addr_lines_.push_back (address (line, source_));
        pc_low_ = rhs.pc_low_;
        pc_high_ = rhs.pc_high_;
        ranges_ = rhs.ranges_;
        die_offset = rhs.die_offset;
      }
      return *this;
    }

    void
    compilation_unit::dump_die ()
    {
      debug_info_entry die (debug, die_offset);
      std::cout << "CU @ 0x" << std::hex << offset_ << std::dec << std::endl;
      die_dump_children (die, "");
    }

    source_flags::source_flags (const std::string& source)
      : source (source)
    {
    }

    bool
    source_flags_compare::operator () (const source_flags& a,
                                       const source_flags& b) const
    {
      if (by_basename)
        return rld::path::basename (a.source) < rld::path::basename (b.source);
      return a.source < b.source;
    }

    source_flags_compare:: source_flags_compare (bool by_basename)
      : by_basename (by_basename)
    {
    }

    producer_source::producer_source (const std::string& producer)
      : producer (producer)
    {
    }

    producer_source::producer_source ()
    {
    }

    file::file ()
      : debug (nullptr),
        elf_ (nullptr)
    {
    }

    file::~file ()
    {
      try
      {
        end ();
      }
      catch (rld::error re)
      {
        std::cerr << "error: rld::dwarf::file::~file: "
                  << re.where << ": " << re.what
                  << std::endl;
      }
      catch (...)
      {
        std::cerr << "error: rld::dwarf::file::~file: unhandled exception"
                  << std::endl;
      }
    }

    void
    file::begin (rld::elf::file& elf__)
    {
      if (rld::verbose () >= RLD_VERBOSE_FULL_DEBUG)
        std::cout << "dwarf::begin: " << elf__.name () << std::endl;

      if (debug != nullptr || elf_ != nullptr)
        throw rld::error ("Already called", "dwarf:file:begin");

      /*
       * DWARF data is not writable.
       */
      if (elf__.is_writable ())
        throw rld::error ("Cannot write DWARF info", "dwarf:file:begin");

      /*
       * Initialise the DWARF instance.
       */
      dwarf_error de;
      int dr = ::dwarf_elf_init (elf__.get_elf (),
                                 DW_DLC_READ,
                                 nullptr,
                                 this,
                                 &debug,
                                 &de);
      libdwarf_error_check ("file:begin", dr, de);

      /*
       * Record the ELF instance and obtain a reference to it. The ELF file
       * cannot end while the DWARF file has not ended.
       */
      elf__.reference_obtain ();
      elf_ = &elf__;
    }

    void
    file::end ()
    {
      if (debug)
      {
        if (rld::verbose () >= RLD_VERBOSE_FULL_DEBUG)
          std::cout << "dwarf::end: " << name () << std::endl;

        cus.clear ();

        ::dwarf_finish (debug, 0);
        if (elf_)
          elf_->reference_release ();
        elf_ = nullptr;
        debug = nullptr;
      }
    }

    void
    file::load_debug ()
    {
      dwarf_unsigned cu_offset = 0;

      while (true)
      {
        dwarf_unsigned cu_next_offset = 0;
        dwarf_error    de;
        int            dr;

        dr = ::dwarf_next_cu_header_c(debug, 1,
                                      nullptr, nullptr, nullptr,  nullptr,
                                      nullptr, nullptr, nullptr,  nullptr,
                                      &cu_next_offset, &de);
        if (dr != DW_DLV_OK)
          break;

        /*
         * Find the CU DIE.
         */
        debug_info_entry die (*this);
        debug_info_entry ret_die (*this);

        while (true)
        {
          dr = ::dwarf_siblingof(debug, die, ret_die, &de);
          if (dr != DW_DLV_OK)
            break;

          if (ret_die.tag () == DW_TAG_compile_unit)
          {
            cus.push_back (compilation_unit (*this, ret_die, cu_offset));
            break;
          }

          die = ret_die;
        }

        cu_offset = cu_next_offset;
      }
    }

    void
    file::load_types ()
    {
      for (auto& cu : cus)
        cu.load_types ();
    }

    void
    file::load_functions ()
    {
      for (auto& cu : cus)
        cu.load_functions ();
    }

    bool
    file::get_source (const unsigned int addr,
                      std::string&       source_file,
                      int&               source_line)
    {
      bool r = false;

      /*
       * Search the CU's collecting the addresses. An address can appear in
       * more than one CU. It may be the last address and the first.
       */
      source_file = "unknown";
      source_line = -1;

      address match;

      for (auto& cu : cus)
      {
        address line;
        r = cu.get_source (addr, line);
        if (r)
        {
          if (match.valid () &&
              (match.is_an_end_sequence () || !!line.is_an_end_sequence ()))
          {
            match = line;
          }
          else
          {
            match = line;
          }
        }
      }

      if (match.valid ())
      {
        source_file = match.path ();
        source_line = match.line ();
        r = true;
      }

      return r;
    }

    bool
    file::get_function (const unsigned int addr,
                        std::string&       name)
    {
      name = "unknown";

      for (auto& cu : cus)
      {
        for (auto& func : cu.get_functions ())
        {
          if (func.inside (addr))
          {
            name = func.name ();
            return true;
          }
        }
      }

      return false;
    }

    void
    file::get_producer_sources (producer_sources& producers)
    {
      for (auto& cu : cus)
      {
        std::string     producer = cu.producer ();
        producer_source new_producer;
        source_flags    sf (cu.name ());
        rld::strings    parts;

        rld::split (parts, producer);

        for (auto& s : parts)
        {
          if (s[0] == '-')
            sf.flags.push_back (s);
          else
            new_producer.producer +=  ' ' + s;
        }

        bool add = true;

        for (auto& p : producers)
        {
          if (p.producer == new_producer.producer)
          {
            p.sources.push_back (sf);
            add = false;
            break;
          }
        }
        if (add)
        {
          new_producer.sources.push_back (sf);
          producers.push_back (new_producer);
        }
      }
    }

    dwarf&
    file::get_debug ()
    {
      return debug;
    }

    compilation_units&
    file::get_cus ()
    {
      return cus;
    }

    const std::string&
    file::name () const
    {
      if (!elf_)
        throw rld::error ("No begin called", "dwarf:fie:name");
      return elf_->name ();
    }

    void
    file::check (const char* where) const
    {
      if (!debug || !elf_)
      {
        std::string w = where;
        throw rld::error ("No DWARF or ELF file", "dwarf:file:" + w);
      }
    }

  }
}