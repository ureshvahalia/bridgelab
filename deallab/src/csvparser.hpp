// The csvParser class consumes a csv format string and returns the comma-separated elements
// one at a time. The class modifies the original string in the process, replacing the commas by null characters
// If users wish to use the original string after calling the parser, they need to make a copy of it first.
class csvParser {
    char* startChar; // for parsing creation string
  public:
    csvParser (char* start);
    char* parseNext (char lim = ',');
    // Return a pointer to the next element
    // Replaces the trailing comma after the element with a NULL character
};
