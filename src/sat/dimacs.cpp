/*++
Copyright (c) 2011 Microsoft Corporation

Module Name:

    dimacs.cpp

Abstract:

    Dimacs CNF parser

Author:

    Leonardo de Moura (leonardo) 2011-07-26.

Revision History:

--*/
#include "sat/dimacs.h"
#undef max
#undef min
#include "sat/sat_solver.h"

template<typename Buffer>
static bool is_whitespace(Buffer & in) {
    return (*in >= 9 && *in <= 13) || *in == 32;
}

template<typename Buffer>
static void skip_whitespace(Buffer & in) {
    while (is_whitespace(in))
        ++in; 
}

template<typename Buffer>
static void skip_line(Buffer & in) {
    while(true) {
        if (*in == EOF) {
            return;
        }
        if (*in == '\n') { 
            ++in; 
            return; 
        }
        ++in; 
    } 
}

template<typename Buffer>
static int parse_int(Buffer & in, std::ostream& err) {
    int     val = 0;
    bool    neg = false;
    skip_whitespace(in);

    if (*in == '-') {
        neg = true;
        ++in;
    }
    else if (*in == '+') {
        ++in;
    }

    if (*in < '0' || *in > '9') {
        if (20 <= *in && *in < 128) 
            err << "(error, \"unexpected char: " << ((char)*in) << " line: " << in.line() << "\")\n";
        else
            err << "(error, \"unexpected char: " << *in << " line: " << in.line() << "\")\n";
        throw dimacs::lex_error();
    }

    while (*in >= '0' && *in <= '9') {
        val = val*10 + (*in - '0');
        ++in;
    }

    return neg ? -val : val; 
}

template<typename Buffer>
static void read_clause(Buffer & in, std::ostream& err, sat::solver & solver, sat::literal_vector & lits) {
    int     parsed_lit;
    int     var;
    
    lits.reset();

    while (true) { 
        parsed_lit = parse_int(in, err);
        if (parsed_lit == 0)
            break;
        var = abs(parsed_lit);
        SASSERT(var > 0);
        while (static_cast<unsigned>(var) >= solver.num_vars())
            solver.mk_var();
        lits.push_back(sat::literal(var, parsed_lit < 0));
    }
}

template<typename Buffer>
static void read_clause(Buffer & in, std::ostream& err, sat::literal_vector & lits) {
    int     parsed_lit;
    int     var;
    
    lits.reset();

    while (true) { 
        parsed_lit = parse_int(in, err);
        if (parsed_lit == 0)
            break;
        var = abs(parsed_lit);
        SASSERT(var > 0);
        lits.push_back(sat::literal(var, parsed_lit < 0));
    }
}



template<typename Buffer>
static bool parse_dimacs_core(Buffer & in, std::ostream& err, sat::solver & solver) {
    sat::literal_vector lits;
    try {
        while (true) {
            skip_whitespace(in);
            if (*in == EOF) {
                break;
            }
            else if (*in == 'c' || *in == 'p') {
                skip_line(in);
            }
            else {
                read_clause(in, err, solver, lits);
                solver.mk_clause(lits.size(), lits.data());
            }
        }
    }
    catch (dimacs::lex_error& ) {
        return false;
    }
    return true;
}


bool parse_dimacs(std::istream & in, std::ostream& err, sat::solver & solver) {
    dimacs::stream_buffer _in(in);
    return parse_dimacs_core(_in, err, solver);
}


namespace dimacs {

    std::ostream& operator<<(std::ostream& out, drat_record const& r) {
        std::function<symbol(int)> fn = [&](int th) { return symbol(th); };
        drat_pp pp(r, fn);
        return out << pp;
    }

    std::ostream& operator<<(std::ostream& out, drat_pp const& p) {
        auto const& r = p.r;
        sat::status_pp pp(r.m_status, p.th);
        return out << pp << " " << r.m_lits << " 0\n";            
    }

    char const* drat_parser::parse_identifier() {
        m_buffer.reset();
        while (!is_whitespace(in)) {
            m_buffer.push_back(*in);
            ++in;
        }
        m_buffer.push_back(0);
        return m_buffer.data();
    }

    char const* drat_parser::parse_quoted_symbol() {
        SASSERT(*in == '|');
        m_buffer.reset();
        m_buffer.push_back(*in);
        bool escape = false;
        ++in;
        while (true) {
            auto c = *in;
            if (c == EOF) 
                throw lex_error();
            else if (c == '\n') 
                ;
            else if (c == '|' && !escape) {
                ++in;
                m_buffer.push_back(c);
                m_buffer.push_back(0);
                return m_buffer.data();
            }
            escape = (c == '\\');
            m_buffer.push_back(c);
            ++in;
        }
    }

    char const* drat_parser::parse_sexpr() {
        if (*in == '|')
            return parse_quoted_symbol();
        m_buffer.reset();
        unsigned lp = 0;
        while (!is_whitespace(in) || lp > 0) {
            m_buffer.push_back(*in);
            if (*in == '(') 
                ++lp;
            else if (*in == ')') {
                if (lp == 0) { 
                    throw lex_error(); 
                } 
                else --lp;
            }
            ++in;
        }
        m_buffer.push_back(0);
        return m_buffer.data();        
    }

    int drat_parser::read_theory_id() {
        skip_whitespace(in);
        if ('a' <= *in && *in <= 'z') {
            if (!m_read_theory_id)
                throw lex_error();
            return m_read_theory_id(parse_identifier());
        }
        else {
            return -1;
        }
    }

    bool drat_parser::next() {
        int theory_id;
        try {
        loop:
            skip_whitespace(in);
            switch (*in) {
            case EOF:
                return false;                
            case 'c':
                // parse comment line
            case 'p':
                // parse meta-data information
                skip_line(in);
                goto loop;
            case 'i':
                // parse input clause
                ++in;
                skip_whitespace(in);
                read_clause(in, err, m_record.m_lits);
                m_record.m_status = sat::status::input();
                break;
            case 'a':
                // parse non-redundant theory clause
                ++in;
                skip_whitespace(in);
                theory_id = read_theory_id();
                skip_whitespace(in);
                read_clause(in, err, m_record.m_lits);
                m_record.m_status = sat::status::th(false, theory_id);
                break;
            case 'd':
                // parse clause deletion
                ++in;
                skip_whitespace(in);
                read_clause(in, err, m_record.m_lits);
                m_record.m_status = sat::status::deleted();
                break;
            case 'r':
                // parse redundant theory clause
                // the clause must be DRUP redundant modulo T
                ++in;
                skip_whitespace(in);
                theory_id = read_theory_id();
                read_clause(in, err, m_record.m_lits);
                m_record.m_status = sat::status::th(true, theory_id);
                break;
            default:
                // parse clause redundant modulo DRAT (or mostly just DRUP)
                read_clause(in, err, m_record.m_lits);
                m_record.m_status = sat::status::redundant();
                break;                
            }    
            return true;
        }
        catch (dimacs::lex_error&) {
            return false;
        }
    }
}
