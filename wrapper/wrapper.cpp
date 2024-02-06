#include "wrapper.hpp"

hfst_ol_tokenize::TokenizeSettings settings;

class membuf : public std::basic_streambuf<char>
{
public:
    membuf(const uint8_t *p, size_t l)
    {
        setg((char *)p, (char *)p, (char *)p + l);
    }
};

class memstream : public std::istream
{
public:
    memstream(const uint8_t *p, size_t l) : std::istream(&_buffer),
                                            _buffer(p, l)
    {
        rdbuf(&_buffer);
    }

private:
    membuf _buffer;
};

void error(int status, int errnum, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (errnum != 0)
    {
        fprintf(stderr, "%s", strerror(errnum));
    }
    if (status != 0)
    {
        exit(status);
    }
}

inline void process_input_0delim_print(hfst_ol::PmatchContainer &container,
                                       std::ostream &outstream,
                                       std::ostringstream &cur)
{
    const std::string &input_text{cur.str()};
    if (!input_text.empty())
    {
        match_and_print(container, outstream, input_text, settings);
    }
    cur.clear();
    cur.str(string());
}

template <bool do_superblank>
int process_input_0delim(hfst_ol::PmatchContainer &container,
                         std::istream &infile, std::ostream &outstream)
{
    bool in_blank = false;
    std::ostringstream cur;

    std::string line;
    // char c;
    while (!infile.eof())
    {
        getline(infile, line, '\0');

        bool escaped = false; // Beginning of line is necessarily unescaped
        for (unsigned long i = 0; i < line.length(); ++i)
        {
            if (escaped)
            {
                cur << line[i];
                escaped = false;
                continue;
            }
            else if (do_superblank && !in_blank && line[i] == '[')
            {
                process_input_0delim_print(container, outstream, cur);
                cur << line[i];
                in_blank = true;
            }
            else if (do_superblank && in_blank && line[i] == ']')
            {
                cur << line[i];
                if (i + 1 < line.length() && line[i + 1] == '[')
                {
                    // Join consecutive superblanks
                    ++i;
                    cur << line[i];
                }
                else
                {
                    in_blank = false;
                    print_nonmatching_sequence(cur.str(), outstream, settings);
                    cur.clear();
                    cur.str(string());
                }
            }
            else if (!in_blank && line[i] == '\n')
            {
                cur << line[i];
                process_input_0delim_print(container, outstream, cur);
            }
            else if (line[i] == '\0')
            {
                process_input_0delim_print(container, outstream, cur);
                outstream << "<STREAMCMD:FLUSH>" << std::endl; // CG format uses this instead of \0
                outstream.flush();
                if (outstream.bad())
                {
                    std::cerr << "hfst-tokenize: Could not flush file" << std::endl;
                }
            }
            else
            {
                cur << line[i];
            }
            escaped = (line[i] == '\\');
        }
    }

    if (in_blank)
    {
        print_nonmatching_sequence(cur.str(), outstream, settings);
    }
    else
    {
        process_input_0delim_print(container, outstream, cur);
    }

    return EXIT_SUCCESS;
}

int process_input(hfst_ol::PmatchContainer &container, std::istream &infile, std::ostream &outstream)
{
    outstream << std::fixed << std::setprecision(10);

    // Processing giellacg without superblanks
    return process_input_0delim<false>(container, infile, outstream);
}

extern "C" const char *hfst_tokenize(const uint8_t *input, size_t input_size, const uint8_t *tokenizer, size_t tokenizer_size)
{
    std::ostringstream output;
    std::string input_str(input, input + input_size);
    std::string tokenizer_filename(tokenizer, tokenizer + tokenizer_size);

    // Settings to output CG format used in Giella infrastructure
    settings.output_format = hfst_ol_tokenize::giellacg;
    settings.print_weights = true;
    settings.print_all = true;
    settings.dedupe = true;
    settings.hack_uncompose = true;
    settings.verbose = false;
    if (settings.max_weight_classes == std::numeric_limits<int>::max())
    {
        settings.max_weight_classes = 2;
    }

    std::ifstream instream(tokenizer_filename, std::ifstream::binary);
    if (!instream.good())
    {
        std::cerr << "Could not open file " << tokenizer_filename << std::endl;
        return "ERR"; // TODO: this
    }

    memstream text(input, input_size);
    if (!instream.good())
    {
        std::cerr << "Could not open file " << tokenizer_filename << std::endl;
        return "ERR"; // TODO: this
    }

    try
    {
        std::map<std::string, std::string> first_header_attributes;
        try
        {
            first_header_attributes = hfst_ol::PmatchContainer::parse_hfst3_header(instream);
            instream.seekg(0);
            instream.clear();
        }
        catch (TransducerHeaderException &err)
        {
            std::cerr << tokenizer_filename
                      << " is not an HFST archive" << std::endl
                      << "Exception thrown:" << std::endl
                      << err.what() << std::endl;
            return "ERR"; // TODO: this
        }

        if (first_header_attributes.count("name") == 0 || first_header_attributes["name"] != "TOP")
        {
            std::cerr << "No TOP automaton found" << std::endl;
            return "ERR"; // TODO: this
        }

        hfst_ol::PmatchContainer container(instream);
        container.set_verbose(false);
        container.set_single_codepoint_tokenization(!settings.tokenize_multichar);

        if (process_input(container, text, output) != EXIT_SUCCESS)
        {
            return "ERR"; // TODO: this
        }

        char* c_str = strdup(output.str().c_str());
        return c_str;
    }
    catch (HfstException &err)
    {
        std::cerr << "Exception thrown:" << std::endl
                  << err.what() << std::endl;
        return "ERR"; // TODO: this
    }
}

extern "C" void hfst_free_cstr(char* c_str) {
    free(c_str);
}