/* Copyright 2017 Fabian Koller
 *
 * This file is part of libopenPMD.
 *
 * libopenPMD is free software: you can redistribute it and/or modify
 * it under the terms of of either the GNU General Public License or
 * the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libopenPMD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License and the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and the GNU Lesser General Public License along with libopenPMD.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#include <ios>
#include <iostream>
#include <regex>
#include <set>

#include <boost/filesystem.hpp>

#include "Auxiliary.hpp"
#include "IO/AbstractIOHandler.hpp"
#include "Series.hpp"


std::ostream&
operator<<(std::ostream& os, IterationEncoding ie)
{
    switch( ie )
    {
        case IterationEncoding::fileBased:
            os<<"fileBased";
            break;
        case IterationEncoding::groupBased:
            os<<"groupBased";
            break;
    }
    return os;
}

Series Series::create(std::string const& path,
                      std::string const& name,
                      IterationEncoding ie,
                      Format f,
                      AccessType at)
{
    return Series(path, name, ie, f, at);
}

Series::Series(std::string const& path,
               std::string const& name,
               IterationEncoding ie,
               Format f,
               AccessType at)
        : iterations{Container< Iteration, uint64_t >()},
          m_name{cleanFilename(name, f)}
{
    std::string cleanPath{path};
    if( !ends_with(cleanPath, "/") )
        cleanPath += '/';

    IOHandler = AbstractIOHandler::createIOHandler(cleanPath, at, f);
    iterations.IOHandler = IOHandler;
    iterations.parent = this;

    switch( at )
    {
        case AccessType::CREAT:
        {
            setOpenPMD(OPENPMD);
            setOpenPMDextension(0);
            setAttribute("basePath", std::string(BASEPATH));
            setMeshesPath("meshes/");
            setParticlesPath("particles/");
            if( ie == IterationEncoding::fileBased && !contains(m_name, "%T") )
                throw std::runtime_error("For fileBased formats the iteration regex %T must be included in the file name");
            setIterationEncoding(ie);
            break;
        }
        case AccessType::READ_ONLY:
        case AccessType::READ_WRITE:
        {
            if( contains(m_name, "%T") )
                readFileBased();
            else
                readGroupBased();
            break;
        }
    }
}

Series Series::read(std::string const& path,
                    std::string const& name,
                    bool readonly,
                    bool parallel)
{
    return Series(path, name, readonly, parallel);
}

Series::Series(std::string path,
               std::string const& name,
               bool readonly,
               bool parallel)
    : iterations{Container< Iteration, uint64_t >()}
{
    if( !ends_with(path, "/") )
        path += '/';
    AccessType at = readonly ? AccessType::READ_ONLY : AccessType::READ_WRITE;
    Format f = Format::NONE;
    if( ends_with(name, ".h5") )
        f = parallel ? Format::PARALLEL_HDF5 : Format::HDF5;
    else if( ends_with(name, ".bp") )
        f = parallel ? Format::PARALLEL_ADIOS : Format::ADIOS;
    IOHandler = AbstractIOHandler::createIOHandler(path, at, f);

    iterations.IOHandler = IOHandler;
    iterations.parent = this;

    m_name = cleanFilename(name, f);

    if( contains(m_name, "%T") )
        readFileBased();
    else
        readGroupBased();
}

Series::~Series()
{
    flush();
    IOHandler->flush();
}

std::string
Series::openPMD() const
{
    return getAttribute("openPMD").get< std::string >();
}

Series&
Series::setOpenPMD(std::string const& o)
{
    setAttribute("openPMD", o);
    dirty = true;
    return *this;
}

uint32_t
Series::openPMDextension() const
{
    return getAttribute("openPMDextension").get< uint32_t >();
}

Series&
Series::setOpenPMDextension(uint32_t oe)
{
    setAttribute("openPMDextension", oe);
    dirty = true;
    return *this;
}

std::string
Series::basePath() const
{
    return getAttribute("basePath").get< std::string >();
}

Series&
Series::setBasePath(std::string const& bp)
{
    std::string version = openPMD();
    if( version == "1.0.0" || version == "1.0.1" )
        throw std::runtime_error("Custom basePath not allowed in openPMD <=1.0.1");

    setAttribute("basePath", bp);
    dirty = true;
    return *this;
}

std::string
Series::meshesPath() const
{
    return getAttribute("meshesPath").get< std::string >();
}

Series&
Series::setMeshesPath(std::string const& mp)
{
    if( ends_with(mp, "/") )
        setAttribute("meshesPath", mp);
    else
        setAttribute("meshesPath", mp + "/");
    dirty = true;
    return *this;
}

std::string
Series::particlesPath() const
{
    return getAttribute("particlesPath").get< std::string >();
}

Series&
Series::setParticlesPath(std::string const& pp)
{
    if( ends_with(pp, "/") )
        setAttribute("particlesPath", pp);
    else
        setAttribute("particlesPath", pp + "/");
    dirty = true;
    return *this;
}

std::string
Series::author() const
{
    return getAttribute("author").get< std::string >();
}

Series&
Series::setAuthor(std::string const& a)
{
    setAttribute("author", a);
    dirty = true;
    return *this;
}

std::string
Series::software() const
{
    return getAttribute("software").get< std::string >();
}

Series&
Series::setSoftware(std::string const& s)
{
    setAttribute("software", s);
    dirty = true;
    return *this;
}

std::string
Series::softwareVersion() const
{
    return getAttribute("softwareVersion").get< std::string >();
}

Series&
Series::setSoftwareVersion(std::string const& sv)
{
    setAttribute("softwareVersion", sv);
    dirty = true;
    return *this;
}

std::string
Series::date() const
{
    return getAttribute("date").get< std::string >();
}

Series&
Series::setDate(std::string const& d)
{
    setAttribute("date", d);
    dirty = true;
    return *this;
}

IterationEncoding
Series::iterationEncoding() const
{
    return m_iterationEncoding;
}

Series&
Series::setIterationEncoding(IterationEncoding ie)
{
    if( written )
        throw std::runtime_error("A files iterationEncoding can not (yet) be changed after it has been written.");

    switch( ie )
    {
        case IterationEncoding::fileBased:
            setIterationFormat(m_name);
            setAttribute("iterationEncoding", std::string("fileBased"));
            break;
        case IterationEncoding::groupBased:
            setIterationFormat(BASEPATH);
            setAttribute("iterationEncoding", std::string("groupBased"));
            break;
    }
    m_iterationEncoding = ie;
    dirty = true;
    return *this;
}

std::string
Series::iterationFormat() const
{
    return getAttribute("iterationFormat").get< std::string >();
}

Series&
Series::setIterationFormat(std::string const& i)
{
    if( written )
        throw std::runtime_error("A files iterationFormat can not (yet) be changed after it has been written.");

    if( m_iterationEncoding == IterationEncoding::groupBased )
    {
        if( basePath() != i && (openPMD() == "1.0.1" || openPMD() == "1.0.0") )
            throw std::invalid_argument("iterationFormat must not differ from basePath " + basePath() + " for groupBased data");
    }
    setAttribute("iterationFormat", i);
    dirty = true;
    return *this;
}

std::string
Series::name() const
{
    return m_name;
}

Series&
Series::setName(std::string const& n)
{
    if( written )
        throw std::runtime_error("A files name can not (yet) be changed after it has been written.");

    if( m_iterationEncoding == IterationEncoding::fileBased && !contains(m_name, "%T") )
            throw std::runtime_error("For fileBased formats the iteration regex %T must be included in the file name");

    m_name = n;
    dirty = true;
    return *this;
}

void
Series::flush()
{
    if( IOHandler->accessType == AccessType::READ_WRITE ||
        IOHandler->accessType == AccessType::CREAT )
    {
        switch( m_iterationEncoding )
        {
            using IE = IterationEncoding;
            case IE::fileBased:
                flushFileBased();
                break;
            case IE::groupBased:
                flushGroupBased();
                break;
        }
    }
}

void
Series::flushFileBased()
{
    if( iterations.empty() )
        throw std::runtime_error("fileBased output can not be written with no iterations.");

    for( auto& i : iterations )
    {
        /* as there is only one series,
         * emulate the file belonging to each iteration as not yet written */
        written = false;
        iterations.written = false;

        i.second.flushFileBased(i.first);

        iterations.flush(replace_first(basePath(), "%T/", ""));

        if( dirty )
        {
            flushAttributes();
            /* manually flag the Series dirty
             * until all iterations have been updated */
            dirty = true;
        }
    }
    dirty = false;
}

void
Series::flushGroupBased()
{
    if( !written )
    {
        Parameter< Operation::CREATE_FILE > file_parameter;
        file_parameter.name = m_name;
        IOHandler->enqueue(IOTask(this, file_parameter));
        IOHandler->flush();
    }

    if( !iterations.written )
        iterations.parent = this;
    iterations.flush(replace_first(basePath(), "%T/", ""));

    for( auto& i : iterations )
    {
        if( !i.second.written )
            i.second.parent = &iterations;
        i.second.flushGroupBased(i.first);
    }

    flushAttributes();
}

void
Series::readFileBased()
{
    std::regex pattern(replace_first(m_name, "%T", "[[:digit:]]+"));

    Parameter< Operation::OPEN_FILE > file_parameter;
    Parameter< Operation::READ_ATT > attribute_parameter;

    using namespace boost::filesystem;
    path dir(IOHandler->directory);
    for( path const& entry : directory_iterator(dir) )
    {
        if( std::regex_search(entry.filename().string(), pattern) )
        {
            file_parameter.name = entry.filename().string();
            IOHandler->enqueue(IOTask(this, file_parameter));
            IOHandler->flush();
            iterations.parent = this;

            /* allow all attributes to be set */
            written = false;

            using DT = Datatype;
            attribute_parameter.name = "iterationEncoding";
            IOHandler->enqueue(IOTask(this, attribute_parameter));
            IOHandler->flush();
            if( *attribute_parameter.dtype == DT::STRING )
            {
                std::string encoding = Attribute(*attribute_parameter.resource).get< std::string >();
                if( encoding == "fileBased" )
                    m_iterationEncoding = IterationEncoding::fileBased;
                else if( encoding == "groupBased" )
                {
                    m_iterationEncoding = IterationEncoding::groupBased;
                    std::cerr << "Series constructor called with iteration regex '%T' suggests loading a "
                              << "time series with fileBased iteration encoding. Loaded file is groupBased.\n";
                } else
                    throw std::runtime_error("Unknown iterationEncoding: " + encoding);
                setAttribute("iterationEncoding", encoding);
            }
            else
                throw std::runtime_error("Unexpected Attribute datatype for 'iterationEncoding'");

            attribute_parameter.name = "iterationFormat";
            IOHandler->enqueue(IOTask(this, attribute_parameter));
            IOHandler->flush();
            if( *attribute_parameter.dtype == DT::STRING )
                setIterationFormat(Attribute(*attribute_parameter.resource).get< std::string >());
            else
                throw std::runtime_error("Unexpected Attribute datatype for 'iterationFormat'");

            read();
        }
    }

    /* this file need not be flushed */
    iterations.written = true;
    written = true;
}

void
Series::readGroupBased()
{
    Parameter< Operation::OPEN_FILE > file_parameter;
    file_parameter.name = m_name;
    IOHandler->enqueue(IOTask(this, file_parameter));
    IOHandler->flush();

    /* allow all attributes to be set */
    written = false;

    using DT = Datatype;
    Parameter< Operation::READ_ATT > attribute_parameter;
    attribute_parameter.name = "iterationEncoding";
    IOHandler->enqueue(IOTask(this, attribute_parameter));
    IOHandler->flush();
    if( *attribute_parameter.dtype == DT::STRING )
    {
        std::string encoding = Attribute(*attribute_parameter.resource).get< std::string >();
        if( encoding == "groupBased" )
            m_iterationEncoding = IterationEncoding::groupBased;
        else if( encoding == "fileBased" )
        {
            m_iterationEncoding = IterationEncoding::fileBased;
            std::cerr << "Series constructor called with explicit iteration suggests loading a "
                      << "single file with groupBased iteration encoding. Loaded file is fileBased.\n";
        } else
            throw std::runtime_error("Unknown iterationEncoding: " + encoding);
        setAttribute("iterationEncoding", encoding);
    }
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'iterationEncoding'");

    attribute_parameter.name = "iterationFormat";
    IOHandler->enqueue(IOTask(this, attribute_parameter));
    IOHandler->flush();
    if( *attribute_parameter.dtype == DT::STRING )
        setIterationFormat(Attribute(*attribute_parameter.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'iterationFormat'");

    /* do not use the public checked version
     * at this point we can guarantee clearing the container won't break anything */
    iterations.clear_unchecked();

    read();

    /* this file need not be flushed */
    iterations.written = true;
    written = true;
}

void
Series::read()
{
    using DT = Datatype;
    Parameter< Operation::READ_ATT > attribute_parameter;

    attribute_parameter.name = "openPMD";
    IOHandler->enqueue(IOTask(this, attribute_parameter));
    IOHandler->flush();
    if( *attribute_parameter.dtype == DT::STRING )
        setOpenPMD(Attribute(*attribute_parameter.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'openPMD'");

    attribute_parameter.name = "openPMDextension";
    IOHandler->enqueue(IOTask(this, attribute_parameter));
    IOHandler->flush();
    if( *attribute_parameter.dtype == DT::UINT32 )
        setOpenPMDextension(Attribute(*attribute_parameter.resource).get< uint32_t >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'openPMDextension'");

    attribute_parameter.name = "basePath";
    IOHandler->enqueue(IOTask(this, attribute_parameter));
    IOHandler->flush();
    if( *attribute_parameter.dtype == DT::STRING )
        setAttribute("basePath", Attribute(*attribute_parameter.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'basePath'");

    attribute_parameter.name = "meshesPath";
    IOHandler->enqueue(IOTask(this, attribute_parameter));
    IOHandler->flush();
    if( *attribute_parameter.dtype == DT::STRING )
        setMeshesPath(Attribute(*attribute_parameter.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'meshesPath'");

    attribute_parameter.name = "particlesPath";
    IOHandler->enqueue(IOTask(this, attribute_parameter));
    IOHandler->flush();
    if( *attribute_parameter.dtype == DT::STRING )
        setParticlesPath(Attribute(*attribute_parameter.resource).get< std::string >());
    else
        throw std::runtime_error("Unexpected Attribute datatype for 'particlesPath'");

    Parameter< Operation::OPEN_PATH > path_parameter;
    std::string version = openPMD();
    if( version == "1.0.0" || version == "1.0.1" )
        path_parameter.path = replace_first(basePath(), "/%T/", "");
    else
        throw std::runtime_error("Unknown openPMD version - " + version);
    IOHandler->enqueue(IOTask(&iterations, path_parameter));
    IOHandler->flush();

    iterations.readAttributes();

    /* obtain all paths inside the basepath (i.e. all iterations) */
    Parameter< Operation::LIST_PATHS > list_parameter;
    IOHandler->enqueue(IOTask(&iterations, list_parameter));
    IOHandler->flush();

    for( auto const& it : *list_parameter.paths )
    {
        Iteration& i = iterations[std::stoull(it)];
        path_parameter.path = it;
        IOHandler->enqueue(IOTask(&i, path_parameter));
        IOHandler->flush();
        i.read();
    }

    readAttributes();
}

std::string
Series::cleanFilename(std::string s, Format f)
{
    switch( f )
    {
        case Format::HDF5:
        case Format::PARALLEL_HDF5:
            if( ends_with(s, ".h5") )
                s = replace_last(s, ".h5", "");
            break;
        case Format::ADIOS:
        case Format::ADIOS2:
        case Format::PARALLEL_ADIOS:
        case Format::PARALLEL_ADIOS2:
            if( ends_with(s, ".bp") )
                s = replace_last(s, ".bp", "");
            break;
        default:
            break;
    }

    return s;
}