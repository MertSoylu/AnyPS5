#ifndef CORE_LIBS_PRX_LIBKERNEL_FILE_DESCRIPTORFLAGS_HPP
#define CORE_LIBS_PRX_LIBKERNEL_FILE_DESCRIPTORFLAGS_HPP

namespace File {

void RecordDescriptorFlags(int fd, int openFlags);
void ForgetDescriptorFlags(int fd);
int Fcntl(int fd, int command, int argument);

}

#endif
