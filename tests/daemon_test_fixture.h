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

#ifndef MULTIPASS_DAEMON_TEST_FIXTURE_H
#define MULTIPASS_DAEMON_TEST_FIXTURE_H

#include "mock_virtual_machine_factory.h"
#include "temp_dir.h"

#include <src/daemon/daemon.h>
#include <src/daemon/daemon_config.h>

#include <multipass/rpc/multipass.grpc.pb.h>

#include <QEventLoop>
#include <QThread>

#include <memory>

using namespace testing;
namespace mp = multipass;

namespace multipass
{
namespace test
{
template <typename W>
class MockServerWriter : public grpc::ServerWriterInterface<W>
{
public:
    MOCK_METHOD0(SendInitialMetadata, void());
    MOCK_METHOD2_T(Write, bool(const W& msg, grpc::WriteOptions options));
};

struct DaemonTestFixture : public ::Test
{
    DaemonTestFixture();

    void SetUp() override;

    MockVirtualMachineFactory* use_a_mock_vm_factory();

    void send_command(const std::vector<std::string>& command, std::ostream& cout = trash_stream,
                      std::ostream& cerr = trash_stream, std::istream& cin = trash_stream);

    void send_commands(std::vector<std::vector<std::string>> commands, std::ostream& cout = trash_stream,
                       std::ostream& cerr = trash_stream, std::istream& cin = trash_stream);

    int total_lines_of_output(std::stringstream& output);

    std::string fake_json_contents(const std::string& default_mac,
                                   const std::vector<mp::NetworkInterface>& extra_ifaces,
                                   const mp::optional<mp::VMMount>& mount = mp::nullopt);

    std::pair<std::unique_ptr<TempDir>, QString> // unique_ptr bypasses missing move ctor
    plant_instance_json(const std::string& contents);

    template <typename R>
    bool is_ready(std::future<R> const& f)
    {
        // 5 seconds should be plenty of time for the work to be complete
        return f.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    }

    /**
     * Helper function to call one of the <em>daemon slots</em> that ultimately handle RPC requests
     *  (e.g. @c mp::Daemon::get). It takes care of promise/future boilerplate. This will generally be given a
     *  @c mpt::MockServerWriter, which can be used to verify replies.
     * @tparam DaemonSlotPtr The method pointer type for the provided slot. Example:
     *  @code
     *  void (mp::Daemon::*)(const mp::GetRequest *, grpc::ServerWriterInterface<mp::GetReply> *,
     *                       std::promise<grpc::Status> *)>
     *  @endcode
     * @tparam Request The request type that the provided slot expects. Example: @c mp::GetRequest
     * @tparam Server The concrete @c grpc::ServerWriterInterface type that the provided slot expects. The template
     * needs to be instantiated with the correct reply type. Example: <tt> grpc::ServerWriterInterface\<mp\::GetReply\>
     * </tt>
     * @param daemon The daemon object to call the slot on.
     * @param slot A pointer to the daemon slot method that should be called.
     * @param request The request to call the slot with.
     * @param server The concrete @c grpc::ServerWriterInterface to call the slot with (see doc on Server typename).
     * This will generally be a @c mpt::MockServerWriter. Notice that this is a <em>universal reference</em>, so it will
     * bind to both lvalue and rvalue references.
     * @return The @c grpc::Status that is produced by the slot
     */
    template <typename DaemonSlotPtr, typename Request, typename Server>
    grpc::Status call_daemon_slot(Daemon& daemon, DaemonSlotPtr slot, const Request& request, Server&& server)
    {
        std::promise<grpc::Status> status_promise;
        auto status_future = status_promise.get_future();

        auto thread = QThread::create([&daemon, slot, &request, &server, &status_promise] {
            QEventLoop loop;
            (daemon.*slot)(&request, &server, &status_promise);
            loop.exec();
        });

        thread->start();

        EXPECT_TRUE(is_ready(status_future));

        thread->quit();

        return status_future.get();
    }

#ifdef MULTIPASS_PLATFORM_WINDOWS
    std::string server_address{"localhost:50051"};
#else
    std::string server_address{"unix:/tmp/test-multipassd.socket"};
#endif
    QEventLoop loop; // needed as signal/slots used internally by mp::Daemon
    TempDir cache_dir;
    TempDir data_dir;
    DaemonConfigBuilder config_builder;
    inline static std::stringstream trash_stream{}; // this may have contents (that we don't care about)
};
} // namespace test
} // namespace multipass
#endif // MULTIPASS_DAEMON_TEST_FIXTURE_H
