 #include <stdio.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <limits.h>  // for PATH_MAX
 #include <string.h>

 #define MIN(X,Y) ((X<Y)?X:Y)
 #define MAX(X,Y) ((X>Y)?X:Y)

 class Client
 {
     friend class ClientList;

     public:
         Client(unsigned long theId);
         unsigned long Id() {return id;}
         unsigned long Sent() {return sent;}
         void SetSent(unsigned long count) {sent = count;}
         unsigned long Suppressed() {return suppressed;}
         void SetSuppressed(unsigned long count) {suppressed = count;}
         Client* Next() {return next;}

     private:
         unsigned long id;
         unsigned long sent;
         unsigned long suppressed;
         Client*       next;
 };



 class ClientList
 {
     public:
         ClientList();
         ~ClientList();
         void AddClient(Client* theClient);
         void Destroy();
         Client* FindClientById(unsigned long theId);
         Client* Top() {return top;}

     private:
         Client* top;
 };


 Client::Client(unsigned long theId)
     : id(theId), sent(0), suppressed(0), next(NULL)
 {
 }

 ClientList::ClientList()
     : top(NULL)
 {
 }

 ClientList::~ClientList()
 {
     Destroy();
 }

 void ClientList::AddClient(Client* theClient)
 {
     theClient->next = top;
     top = theClient;
 }  // end ClientList::AddClient()

 void ClientList::Destroy()
 {
     while (top)
     {
         Client* next = top->next;
         delete top;
         top = next;   
     }
 }  // end ClientList::Destroy()

 Client* ClientList::FindClientById(unsigned long theId)
 {
     Client* next = top;
     while(next)
     {
         if (theId == next->id)
             return next;
         else
             next = next->next;
     }       
     return NULL;
 }  // end ClientList::FindClientById()




 const int MAX_LINE = 256;

 class FastReader
 {
     public:
         FastReader();
         bool Read(FILE* filePtr, char* buffer, unsigned int* len);
         bool Readline(FILE* filePtr, char* buffer, unsigned int* len);

     private:
         char         savebuf[256];
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
         unsigned int result = fread(savebuf, sizeof(char), 256, filePtr);
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
     ClientList list;
     FastReader reader;
     char buffer[512];
     unsigned long line = 0;
     unsigned int len = 512;
     while (reader.Readline(stdin, buffer, &len))
     {
         line++;
         if (!strncmp(buffer, "Report", 6))
         {
             char* ptr = strstr(buffer, "node>");
             if (!ptr) break;
             unsigned long nodeId;
             if (1 != sscanf(ptr, "node>%lu", &nodeId))
             {
                 fprintf(stderr, "nackCount: Error parsing log file "
                                 "at line: %lu\n", line);
                 exit(-1);  
             }
             // Update NACKs transmitted:
             len = 512;
             while (reader.Readline(stdin, buffer, &len))
             {
                 line++;
                 if ('*' == buffer[0]) break;  // end of report
                 len = 512;
                 //fprintf(stderr, "Searching: \"%s\"\n", buffer);
                 ptr = strstr(buffer, "nacks>");
                 if (ptr)
                 {                    
                     Client* client = list.FindClientById(nodeId);
                     if (!client)
                     {
                         if (!(client = new Client(nodeId)))
                         {
                             perror("nackCount: Error creating Client:");
                             exit(-1);   
                         }
                         list.AddClient(client);
                     }
                     unsigned long count;
                     if (1 == sscanf(ptr, "nacks>%lu", &count))
                     {
                         client->SetSent(count);
                     }
                     else
                     {
                        fprintf(stderr, "Error parsing log file "
                                        "at line: %lu  \n", line);
                        exit(-1); 
                     }
                     if ((ptr = strstr(buffer, "suppressed>")))
                     {
                         if (1 == sscanf(ptr, "suppressed>%lu", &count))
                         {
                             client->SetSuppressed(count);
                             break;   
                         }
                     }
                     else
                     {
                         fprintf(stderr, "Error parsing log file "
                                         "at line: %lu  \n", line);
                         exit(-1);
                     }
                 }
             }  // end while (reader.Readline("nacks/suppressed" search))
         }
         len = 512;
     }  // end while (reader.Readline("Report" search))

     unsigned long numClient = 0;
     Client* next = list.Top();
     unsigned long totalSent = 0;
     unsigned long totalSuppressed = 0;
     while (next)
     {
         totalSent += next->Sent();
         totalSuppressed += next->Suppressed(); 
         next = next->Next(); 
         numClient++;
     }
     
     fprintf(stdout, "Receivers:%lu Sent:%lu Suppressed:%lu alpha:%f\n", 
                      numClient, totalSent, totalSuppressed, 
                      (double)totalSent / (double) (totalSuppressed+totalSent));
}  // end main()

