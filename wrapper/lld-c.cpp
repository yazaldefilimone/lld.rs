#include <cstdlib>
#include <iostream>
#include <lld/Common/Driver.h>
#include <lld>
#include <mutex>

// Aloca memória para uma string retornada
const char *allocate_string(const std::string &message) {
  if (message.empty())
    return nullptr;
  char *string_pointer = (char *)malloc(message.size() + 1);
  memcpy(string_pointer, message.c_str(), message.size() + 1);
  return string_pointer;
}

// Mutex global para evitar chamadas concorrentes ao linker
std::mutex linker_mutex;

extern "C" {

// Definição dos tipos de linkers suportados
enum LinkerFlavor {
  ELF = 0,
  WASM = 1,
  MACHO = 2,
  COFF = 3,
};

// Estrutura para armazenar o resultado do linker
struct LinkerResult {
  bool success;
  const char *messages;
};

// Libera a memória do resultado do linker
void free_linker_result(LinkerResult *result) {
  if (result->messages) {
    free((void *)result->messages);
  }
}

// Retorna a função do linker correspondente ao formato desejado
auto select_linker_function(LinkerFlavor flavor) {
  switch (flavor) {
  case WASM:
    return lld::wasm::link;
  case MACHO:
    return lld::macho::link;
  case COFF:
    return lld::coff::link;
  case ELF:
  default:
    return lld::elf::link;
  }
}

// Executa o linker do LLD para o formato desejado
LinkerResult invoke_lld_linker(LinkerFlavor flavor, int argument_count,
                               const char *const *arguments) {
  LinkerResult result;
  auto linker_function = select_linker_function(flavor);

  // Buffers para capturar saída padrão e erros
  std::string output_buffer, error_buffer;
  llvm::raw_string_ostream output_stream(output_buffer);
  llvm::raw_string_ostream error_stream(error_buffer);

  // Copia argumentos para um vetor manipulável
  std::vector<const char *> argument_vector(arguments,
                                            arguments + argument_count);

// Define o nome do executável correto para cada plataforma
#ifdef _WIN32
  if (flavor == COFF) {
    argument_vector.insert(argument_vector.begin(), "lld-link");
  } else {
    argument_vector.insert(argument_vector.begin(), "lld");
  }
#elif __APPLE__
  if (flavor == MACHO) {
    argument_vector.insert(argument_vector.begin(), "ld64.lld");
  } else {
    argument_vector.insert(argument_vector.begin(), "lld");
  }
#else
  argument_vector.insert(argument_vector.begin(), "lld");
#endif

  // Bloqueia execução concorrente do linker
  std::unique_lock<std::mutex> lock(linker_mutex);
  result.success = linker_function(argument_vector, output_stream, error_stream,
                                   false, false);

  // Libera contexto global do LLD
  lld::CommonLinkerContext::destroy();

  // Captura mensagens de erro e saída do linker
  std::string result_message = error_stream.str() + output_stream.str();
  result.messages = allocate_string(result_message);
  return result;
}
}
