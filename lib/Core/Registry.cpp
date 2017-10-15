//===- lib/Core/Registry.cpp - TAPI Registry --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the TAPI Registry.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/Registry.h"
#include "tapi/Core/ConfigurationFile.h"
#include "tapi/Core/MachODylibReader.h"
#include "tapi/Core/ReexportFileWriter.h"
#include "tapi/Core/TextAPI_v1.h"
#include "tapi/Core/TextStub_v1.h"
#include "tapi/Core/TextStub_v2.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

bool Registry::canRead(MemoryBufferRef memBuffer, FileType types) const {
  auto data = memBuffer.getBuffer();
  auto magic = identify_magic(data);

  for (const auto &reader : _readers) {
    if (reader->canRead(magic, memBuffer, types))
      return true;
  }

  return false;
}

Expected<FileType> Registry::getFileType(MemoryBufferRef memBuffer) const {
  auto data = memBuffer.getBuffer();
  auto magic = identify_magic(data);

  for (const auto &reader : _readers) {
    auto fileType = reader->getFileType(magic, memBuffer);
    if (!fileType)
      return fileType.takeError();
    if (fileType.get() != FileType::Invalid)
      return fileType;
  }

  return FileType::Invalid;
}

bool Registry::canWrite(const File *file) const {
  for (const auto &writer : _writers) {
    if (writer->canWrite(file))
      return true;
  }

  return false;
}

Expected<std::unique_ptr<File>>
Registry::readFile(std::unique_ptr<MemoryBuffer> memBuffer, ReadFlags readFlags,
                   ArchitectureSet arches) const {
  auto data = memBuffer->getBuffer();
  auto fileType = identify_magic(data);

  for (const auto &reader : _readers) {
    if (!reader->canRead(fileType, memBuffer->getMemBufferRef()))
      continue;
    return reader->readFile(std::move(memBuffer), readFlags, arches);
  }

  return make_error<StringError>(
      "unsupported file type", std::make_error_code(std::errc::not_supported));
}

Error Registry::writeFile(const File *file) const {
  std::error_code ec;
  raw_fd_ostream os(file->getPath(), ec, sys::fs::F_Text);
  if (ec)
    return errorCodeToError(ec);
  auto error = writeFile(os, file);
  if (error)
    return error;
  os.close();
  if (ec)
    return errorCodeToError(ec);
  return Error::success();
}

Error Registry::writeFile(raw_ostream &os, const File *file) const {
  for (const auto &writer : _writers) {
    if (!writer->canWrite(file))
      continue;
    return writer->writeFile(os, file);
  }

  return make_error<StringError>(
      "unsupported file type", std::make_error_code(std::errc::not_supported));
}

void Registry::addBinaryReaders() {
  add(std::unique_ptr<Reader>(new MachODylibReader));
}

void Registry::addYAMLReaders() {
  auto reader = llvm::make_unique<YAMLReader>();
  reader->add(
      std::unique_ptr<DocumentHandler>(new stub::v1::YAMLDocumentHandler));
  reader->add(
      std::unique_ptr<DocumentHandler>(new stub::v2::YAMLDocumentHandler));
  reader->add(
      std::unique_ptr<DocumentHandler>(new api::v1::YAMLDocumentHandler));
  reader->add(std::unique_ptr<DocumentHandler>(
      new configuration::v1::YAMLDocumentHandler));
  add(std::unique_ptr<Reader>(std::move(reader)));
}

void Registry::addYAMLWriters() {
  auto writer = llvm::make_unique<YAMLWriter>();
  writer->add(
      std::unique_ptr<DocumentHandler>(new stub::v1::YAMLDocumentHandler));
  writer->add(
      std::unique_ptr<DocumentHandler>(new stub::v2::YAMLDocumentHandler));
  writer->add(
      std::unique_ptr<DocumentHandler>(new api::v1::YAMLDocumentHandler));
  add(std::unique_ptr<Writer>(std::move(writer)));
}

void Registry::addReexportWriters() {
  add(std::unique_ptr<Writer>(new ReexportFileWriter));
}

TAPI_NAMESPACE_INTERNAL_END
