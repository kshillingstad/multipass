/*
 * Copyright (C) 2021-2022 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "alias.h"
#include "common_cli.h"

#include <multipass/cli/argparser.h>
#include <multipass/platform.h>

#include <QDir>
#include <QtGlobal>

namespace mp = multipass;
namespace cmd = multipass::cmd;
using RpcMethod = mp::Rpc::Stub;

mp::ReturnCode cmd::Alias::run(mp::ArgParser* parser)
{
    auto ret = parse_args(parser);
    if (ret != ParseCode::Ok)
    {
        return parser->returnCodeFrom(ret);
    }

    try
    {
        MP_PLATFORM.create_alias_script(alias_name, alias_definition);
    }
    catch (std::runtime_error& e)
    {
        cerr << fmt::format("Error when creating script for alias: {}\n", e.what());
        return ReturnCode::CommandLineError;
    }

    bool empty_before_add = aliases.empty();

    aliases.add_alias(alias_name, alias_definition);

#ifdef MULTIPASS_PLATFORM_WINDOWS
    QChar separator(';');
#else
    QChar separator(':');
#endif

    // Each element of this list is a folder in the system's path.
    auto path = qEnvironmentVariable("PATH").split(separator);

    auto alias_folder = MP_PLATFORM.get_alias_scripts_folder().absolutePath();

    if (empty_before_add && aliases.size() == 1 && std::find(path.cbegin(), path.cend(), alias_folder) == path.cend())
        cout << MP_PLATFORM.alias_path_message();

    return ReturnCode::Ok;
}

std::string cmd::Alias::name() const
{
    return "alias";
}

QString cmd::Alias::short_help() const
{
    return QStringLiteral("Create an alias");
}

QString cmd::Alias::description() const
{
    return QStringLiteral("Create an alias to be executed on a given instance.");
}

mp::ParseCode cmd::Alias::parse_args(mp::ArgParser* parser)
{
    parser->addPositionalArgument("definition", "Alias definition in the form <instance>:<command>", "<definition>");
    parser->addPositionalArgument("name", "Name given to the alias being defined, defaults to <command>", "[<name>]");

    auto status = parser->commandParse(this);
    if (status != ParseCode::Ok)
        return status;

    if (parser->positionalArguments().count() != 1 && parser->positionalArguments().count() != 2)
    {
        cerr << "Wrong number of arguments given\n";
        return ParseCode::CommandLineError;
    }

    QStringList cl_definition = parser->positionalArguments();

    QString definition = cl_definition[0];

    auto colon_pos = definition.indexOf(':');

    if (colon_pos == -1 || colon_pos == definition.size() - 1)
    {
        cerr << "No command given\n";
        return ParseCode::CommandLineError;
    }
    if (colon_pos == 0)
    {
        cerr << "No instance name given\n";
        return ParseCode::CommandLineError;
    }

    auto command = definition.right(definition.size() - colon_pos - 1).toStdString();

    if (parser->positionalArguments().count() == 1)
    {
        alias_name = QFileInfo(QString::fromStdString(command)).fileName().toStdString();
    }
    else
    {
        alias_name = cl_definition[1].toStdString();
        if (QFileInfo(cl_definition[1]).fileName().toStdString() != alias_name)
        {
            cerr << "Alias has to be a valid filename" << std::endl;
            return ParseCode::CommandLineError;
        }
    }

    auto instance = definition.left(colon_pos).toStdString();

    info_request.mutable_instance_names()->add_instance_name(instance);
    info_request.set_verbosity_level(0);

    auto on_success = [](InfoReply&) { return ReturnCode::Ok; };

    auto on_failure = [](grpc::Status& status) {
        return status.error_code() == grpc::StatusCode::INVALID_ARGUMENT ? ReturnCode::CommandLineError
                                                                         : ReturnCode::DaemonFail;
    };

    auto ret = dispatch(&RpcMethod::info, info_request, on_success, on_failure);

    if (ret == ReturnCode::DaemonFail)
    {
        cerr << "Error retrieving list of instances\n";
        return ParseCode::CommandLineError;
    }

    if (ret == ReturnCode::CommandLineError)
    {
        cerr << fmt::format("Instance '{}' does not exist\n", instance);
        return ParseCode::CommandLineError;
    }

    if (aliases.get_alias(alias_name))
    {
        cerr << fmt::format("Alias '{}' already exists\n", alias_name);
        return ParseCode::CommandLineError;
    }

    if (parser->findCommand(QString::fromStdString(alias_name)))
    {
        cerr << fmt::format("Alias name '{}' clashes with a command name\n", alias_name);
        return ParseCode::CommandLineError;
    }

    alias_definition = AliasDefinition{instance, command};

    return ParseCode::Ok;
}