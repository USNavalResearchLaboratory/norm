 #include <stdio.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <limits.h>  // for PATH_MAX
 #include <string.h>

 #define MIN(X,Y) ((X<Y)?X:Y)
 #define MAX(X,Y) ((X>Y)?X:Y)

 const int MAX_LINE = 256;

 class FastReader
 {
     public:
         FastReader();
         bool Read(FILE* filePtr, char* buffer, unsigned int* len);
         bool Readline(FILE* filePtr, char* buffer, unsigned int* len);

     private:
         char         savebuf[MAX_LINE];
         char*        saveptr;
         unsigned int savecount;
 };  // end class FastReader

 FastReader::FastReader()
     : savecount(0)
 {

 }

 bool FastReader::Read(FILE* filePtr, char* buffer, unsigned int* len)
 {
     unsigned int want = *len;   
     if (savecount)
     {
         unsigned int ncopy = MIN(want, savecount);
         memcpy(buffer, saveptr, ncopy);
         savecount -= ncopy;
         saveptr += ncopy;
         buffer += ncopy;
         want -= ncopy;
     }
     while (want)
     {
         unsigned int result = fread(savebuf, sizeof(char), MAX_LINE, filePtr);
         if (result)
         {
             unsigned int ncopy= MIN(want, result);
             memcpy(buffer, savebuf, ncopy);
             savecount = result - ncopy;
             saveptr = savebuf + ncopy;
             buffer += ncopy;
             want -= ncopy;
         }
         else  // end-of-file
         {
             *len -= want;
             if (*len)
                 return true;  // we read something
             else
                 return false; // we read nothing
         }
     }
     return true;
 }  // end FastReader::Read()

 // An OK text readline() routine (reads what will fit into buffer incl. NULL termination)
 // if *len is unchanged on return, it means the line is bigger than the buffer and 
 // requires multiple reads

bool FastReader::Readline(FILE* filePtr, char* buffer, unsigned int* len)
{   
     unsigned int count = 0;
     unsigned int length = *len;
     char* ptr = buffer;
     unsigned int one = 1;
     while ((count < length) && Read(filePtr, ptr, &one))
     {
         if (('\n' == *ptr) || ('\r' == *ptr))
         {
             *ptr = '\0';
             *len = count;
             return true;
         }
         count++;
         ptr++;
     }
     // Either we've filled the buffer or hit end-of-file
     if (count < length) *len = 0; // Set *len = 0 on EOF
     return false;
}  // end FastReader::Readline()

int main(int argc, char* argv[])
{
     FastReader reader;
     char buffer[MAX_LINE];
     unsigned long line = 0;
     unsigned int len = MAX_LINE;
     const char* header = "NormSession::ServerUpdateGroupSize";
     unsigned int headerLen = strlen(header);
     
     double totalSize = 0.0;
     unsigned long sizeCount = 0;
     while (reader.Readline(stdin, buffer, &len))
     {
         line++;
         if (!strncmp(buffer, header, headerLen))
         {
             char* ptr = strstr(buffer, "size:");
             if (!ptr) break;
             double size;
             if (1 != sscanf(ptr, "size: %lf", &size))
             {
                 fprintf(stderr, "sizeAve: Error parsing log file "
                                 "at line: %lu\n", line);
                 exit(-1);  
             }
             totalSize += size;
             sizeCount++;
         }
         len = MAX_LINE;
     }  // end while (reader.Readline("Report" search))

     double sizeAve = 0.0;
     if (sizeCount)
     {
         sizeAve = totalSize / (double)sizeCount;
     }
     
     fprintf(stdout, "Average group size estimate:%lf\n", sizeAve);
}  // end main()

