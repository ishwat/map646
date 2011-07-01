#ifndef STAT_FILE_H
#define STAT_FILE_H

#include <string>
#include <fstream>
#include <json/json.h>
#include "date.h"

class stat_file{
   private:
      std::string filename;
      std::string dirname;
      date filedate;
   public:
      std::fstream fs;
      stat_file(){} 
      stat_file(std::string filename0, std::string dirname0);
      
      bool operator==(stat_file& rhs);
      bool operator<(stat_file& rhs);
      bool operator>(stat_file& rhs);
      
      std::string get_filename();
      date get_date();
      json_object* get_jobj();
      is_included(date d);
      int open();
      int close();
};

#endif
