#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* These definitions deliberately replace Mach entirely, including its types. */
#define IOS_THREAD_PRECEDENCE_MOCK_MACH 1
typedef unsigned int mach_port_t, mach_port_name_t, mach_msg_type_number_t, mach_msg_type_name_t;
typedef int kern_return_t, boolean_t;
typedef int *thread_info_t, *thread_policy_t;
struct thread_identifier_info { unsigned long long thread_id, thread_handle, dispatch_qaddr; };
struct thread_precedence_policy { int importance; };
enum {
    KERN_SUCCESS = 0, KERN_PROTECTION_FAILURE = 2, KERN_INVALID_ARGUMENT = 4,
    KERN_NO_ACCESS = 8, KERN_INVALID_NAME = 15, KERN_INVALID_RIGHT = 17,
    KERN_TERMINATED = 37, KERN_NOT_SUPPORTED = 46,
    THREAD_IDENTIFIER_INFO = 4, THREAD_IDENTIFIER_INFO_COUNT = 6,
    THREAD_PRECEDENCE_POLICY = 3, THREAD_PRECEDENCE_POLICY_COUNT = 1,
    MACH_MSG_TYPE_COPY_SEND = 19, MACH_MSG_TYPE_PORT_SEND = 17, FALSE = 0,
};
#define MACH_PORT_NULL 0U
#define MACH_PORT_DEAD UINT32_MAX
#define STATUS_SUCCESS UINT32_C(0x00000000)
#define STATUS_INVALID_HANDLE UINT32_C(0xc0000008)
#define STATUS_INVALID_PARAMETER UINT32_C(0xc000000d)
#define STATUS_ACCESS_DENIED UINT32_C(0xc0000022)
#define STATUS_THREAD_IS_TERMINATING UINT32_C(0xc000004b)
#define STATUS_NOT_SUPPORTED UINT32_C(0xc00000bb)
#define STATUS_UNSUCCESSFUL UINT32_C(0xc0000001)

static unsigned int assertions, cases;
static const char *case_name;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s [%s]\n", __FILE__, __LINE__, #condition, case_name); exit(1); } } while (0)

typedef struct {
    kern_return_t error;
    mach_msg_type_number_t count;
    boolean_t defaults;
    int override_importance, importance;
} GetReply;
typedef struct {
    kern_return_t extract_error, info_error, release_error, set_error[4];
    mach_msg_type_name_t extracted_type;
    mach_port_t extracted_port, expected_port;
    mach_msg_type_number_t info_count;
    unsigned long long identity;
    int policy, set_values[4], owned_refs, original_refs;
    unsigned int task_calls, extract_calls, info_calls, get_calls, set_calls, release_calls;
    GetReply get[4];
} MockMach;
static MockMach mock;

static void begin_case(const char *name)
{
    memset(&mock, 0, sizeof(mock));
    case_name = name;
    ++cases;
    mock.extracted_type = MACH_MSG_TYPE_PORT_SEND;
    mock.extracted_port = mock.expected_port = 77;
    mock.info_count = THREAD_IDENTIFIER_INFO_COUNT;
    mock.identity = 0x123456789abcdef0ULL;
    mock.policy = 11;
    mock.original_refs = 1;
    for (unsigned int i = 0; i < 4; ++i) mock.get[i].count = THREAD_PRECEDENCE_POLICY_COUNT;
}

static mach_port_t mock_mach_task_self(void) { ++mock.task_calls; return 101; }
static kern_return_t mock_mach_port_extract_right(mach_port_t task, mach_port_name_t name,
    mach_msg_type_name_t disposition, mach_port_t *port, mach_msg_type_name_t *type)
{
    ++mock.extract_calls;
    CHECK(task == 101 && name == 99 && disposition == MACH_MSG_TYPE_COPY_SEND);
    if (mock.extract_error) return mock.extract_error;
    *port = mock.extracted_port;
    *type = mock.extracted_type;
    mock.expected_port = *port;
    if (*port) ++mock.owned_refs;
    return KERN_SUCCESS;
}
static kern_return_t mock_mach_port_deallocate(mach_port_t task, mach_port_t port)
{
    ++mock.release_calls;
    CHECK(task == 101 && port == mock.expected_port && mock.owned_refs == 1);
    if (mock.release_error) return mock.release_error;
    --mock.owned_refs;
    return KERN_SUCCESS;
}
static kern_return_t mock_thread_info(mach_port_t port, int flavor, thread_info_t info,
    mach_msg_type_number_t *count)
{
    ++mock.info_calls;
    CHECK(port == mock.expected_port && flavor == THREAD_IDENTIFIER_INFO && *count == THREAD_IDENTIFIER_INFO_COUNT);
    if (mock.info_error) return mock.info_error;
    ((struct thread_identifier_info *)info)->thread_id = mock.identity;
    *count = mock.info_count;
    return KERN_SUCCESS;
}
static kern_return_t mock_thread_policy_get(mach_port_t port, int flavor, thread_policy_t policy,
    mach_msg_type_number_t *count, boolean_t *defaults)
{
    CHECK(mock.get_calls < 4);
    GetReply *reply = &mock.get[mock.get_calls++];
    CHECK(port == mock.expected_port && flavor == THREAD_PRECEDENCE_POLICY &&
          *count == THREAD_PRECEDENCE_POLICY_COUNT && *defaults == FALSE);
    if (reply->error) return reply->error;
    *count = reply->count;
    *defaults = reply->defaults;
    ((struct thread_precedence_policy *)policy)->importance = reply->override_importance ? reply->importance : mock.policy;
    return KERN_SUCCESS;
}
static kern_return_t mock_thread_policy_set(mach_port_t port, int flavor, thread_policy_t policy,
    mach_msg_type_number_t count)
{
    CHECK(mock.set_calls < 4);
    unsigned int index = mock.set_calls++;
    CHECK(port == mock.expected_port && flavor == THREAD_PRECEDENCE_POLICY && count == THREAD_PRECEDENCE_POLICY_COUNT);
    mock.set_values[index] = ((struct thread_precedence_policy *)policy)->importance;
    if (mock.set_error[index]) return mock.set_error[index];
    mock.policy = mock.set_values[index];
    return KERN_SUCCESS;
}
#define mach_task_self mock_mach_task_self
#define mach_port_extract_right mock_mach_port_extract_right
#define mach_port_deallocate mock_mach_port_deallocate
#define thread_info mock_thread_info
#define thread_policy_get mock_thread_policy_get
#define thread_policy_set mock_thread_policy_set
#include "ios_thread_precedence.h"

static void acquire(mach_port_t *port, unsigned long long *identity)
{
    CHECK(ios_precedence_retain(99, port, identity) == STATUS_SUCCESS);
    CHECK(*port == mock.extracted_port && *identity == mock.identity && mock.owned_refs == 1);
}
static void clean(mach_port_t *port)
{
    CHECK(ios_precedence_release(port) == STATUS_SUCCESS);
    CHECK(*port == MACH_PORT_NULL && mock.owned_refs == 0 && mock.original_refs == 1);
    unsigned int before = mock.release_calls;
    CHECK(ios_precedence_release(port) == STATUS_SUCCESS && mock.release_calls == before);
}
static void expect_no_apply(void)
{
    CHECK(mock.get_calls == 0 && mock.set_calls == 0 && mock.policy == 11);
}

static void successful_lifetime(void)
{
    begin_case("retained-right-lifetime-no-late-lookup");
    mach_port_t port = 0;
    unsigned long long identity = 0;
    acquire(&port, &identity);
    CHECK(ios_precedence_apply(port, identity, -31) == STATUS_SUCCESS && mock.policy == -31);
    CHECK(ios_precedence_apply(port, identity, 32) == STATUS_SUCCESS && mock.policy == 32);
    CHECK(mock.extract_calls == 1 && mock.get_calls == 4 && mock.set_calls == 2);
    CHECK(mock.info_calls == 3 && mock.task_calls == 1);
    clean(&port);
    begin_case("same-name-copy-send-still-owns-one-extra-reference");
    mock.extracted_port = 99;
    port = 0; identity = 0;
    acquire(&port, &identity);
    CHECK(port == 99 && mock.original_refs + mock.owned_refs == 2);
    clean(&port);
    CHECK(mock.release_calls == 1 && mock.original_refs == 1);
}

static void invalid_inputs(void)
{
    begin_case("invalid-retain-no-lookup");
    mach_port_t port = 0;
    unsigned long long identity = 42;
    CHECK(ios_precedence_retain(-1, &port, &identity) == STATUS_INVALID_HANDLE);
    CHECK(ios_precedence_retain(0, &port, &identity) == STATUS_INVALID_HANDLE);
    port = 77;
    CHECK(ios_precedence_retain(99, &port, &identity) == STATUS_INVALID_HANDLE);
    CHECK(port == 77 && identity == 42 && mock.extract_calls == 0 && mock.task_calls == 0);
    begin_case("invalid-apply-no-mach-call");
    CHECK(ios_precedence_apply(0, mock.identity, 0) == STATUS_INVALID_HANDLE);
    CHECK(ios_precedence_apply(MACH_PORT_DEAD, mock.identity, 0) == STATUS_INVALID_HANDLE);
    CHECK(ios_precedence_apply(77, 0, 0) == STATUS_INVALID_HANDLE);
    CHECK(ios_precedence_apply(77, mock.identity, -32) == STATUS_NOT_SUPPORTED);
    CHECK(ios_precedence_apply(77, mock.identity, 33) == STATUS_NOT_SUPPORTED);
    CHECK(mock.info_calls == 0 && mock.task_calls == 0 && mock.extract_calls == 0);
    expect_no_apply();
}

static const struct { kern_return_t error; unsigned int status; } errors[] = {
    {KERN_NO_ACCESS, STATUS_ACCESS_DENIED}, {KERN_PROTECTION_FAILURE, STATUS_ACCESS_DENIED},
    {KERN_INVALID_ARGUMENT, STATUS_INVALID_PARAMETER}, {KERN_TERMINATED, STATUS_THREAD_IS_TERMINATING},
    {KERN_INVALID_NAME, STATUS_THREAD_IS_TERMINATING}, {KERN_INVALID_RIGHT, STATUS_THREAD_IS_TERMINATING},
    {KERN_NOT_SUPPORTED, STATUS_NOT_SUPPORTED}, {99, STATUS_UNSUCCESSFUL},
};
static void error_mapping(void)
{
    for (unsigned int i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i)
    {
        begin_case("extract-failure-status-and-no-ownership");
        mach_port_t port = 0;
        unsigned long long identity = 42;
        mock.extract_error = errors[i].error;
        CHECK(ios_precedence_retain(99, &port, &identity) == errors[i].status);
        CHECK(port == 0 && identity == 42 && mock.owned_refs == 0 && mock.info_calls == 0);
        expect_no_apply();
        begin_case("identity-query-failure-retains-until-cleanup");
        mock.info_error = errors[i].error;
        CHECK(ios_precedence_retain(99, &port, &identity) == errors[i].status);
        CHECK(port == 77 && identity == 42 && mock.owned_refs == 1);
        CHECK(ios_precedence_retain(99, &port, &identity) == STATUS_INVALID_HANDLE && mock.extract_calls == 1);
        clean(&port);
        begin_case("apply-identity-query-failure-no-policy-mutation");
        mock.info_error = errors[i].error;
        CHECK(ios_precedence_apply(77, mock.identity, 7) == errors[i].status);
        expect_no_apply();
        begin_case("initial-policy-query-failure-no-policy-mutation");
        mock.get[0].error = errors[i].error;
        CHECK(ios_precedence_apply(77, mock.identity, 7) == errors[i].status);
        CHECK(mock.get_calls == 1 && mock.set_calls == 0 && mock.policy == 11);
        begin_case("policy-set-failure-is-not-success");
        mock.set_error[0] = errors[i].error;
        CHECK(ios_precedence_apply(77, mock.identity, 7) == errors[i].status);
        CHECK(mock.get_calls == 1 && mock.set_calls == 1 && mock.policy == 11);
        begin_case("readback-error-restores-prior-and-preserves-failure-status");
        mock.get[1].error = errors[i].error;
        CHECK(ios_precedence_apply(77, mock.identity, 7) == errors[i].status);
        CHECK(mock.set_calls == 2 && mock.get_calls == 3 && mock.policy == 11);
        CHECK(mock.set_values[0] == 7 && mock.set_values[1] == 11);
        begin_case("release-error-keeps-owned-right-for-retry");
        port = 0; identity = 0;
        acquire(&port, &identity);
        mock.release_error = errors[i].error;
        CHECK(ios_precedence_release(&port) == errors[i].status && port == 77 && mock.owned_refs == 1);
        mock.release_error = KERN_SUCCESS;
        clean(&port);
        CHECK(mock.release_calls == 2);
    }
}

static void malformed_identity(void)
{
    for (unsigned int variant = 0; variant < 6; ++variant)
    {
        begin_case("malformed-retain-keeps-output-for-destruction");
        mach_port_t port = 0;
        unsigned long long identity = 42;
        if (variant == 0) mock.extracted_type = 0;
        if (variant == 1) mock.extracted_port = 0;
        if (variant == 2) mock.extracted_port = MACH_PORT_DEAD;
        if (variant == 3) mock.info_count = THREAD_IDENTIFIER_INFO_COUNT - 1;
        if (variant == 4) mock.info_count = THREAD_IDENTIFIER_INFO_COUNT + 1;
        if (variant == 5) mock.identity = 0;
        CHECK(ios_precedence_retain(99, &port, &identity) == STATUS_INVALID_HANDLE);
        CHECK(port == mock.extracted_port && identity == 42 && mock.extract_calls == 1);
        CHECK(mock.info_calls == (variant < 3 ? 0U : 1U));
        if (port)
        {
            CHECK(ios_precedence_retain(99, &port, &identity) == STATUS_INVALID_HANDLE && mock.extract_calls == 1);
            clean(&port);
        }
        else CHECK(ios_precedence_release(&port) == STATUS_SUCCESS && mock.release_calls == 0);
    }
    for (unsigned int variant = 0; variant < 3; ++variant)
    {
        begin_case("apply-rejects-recycled-identity-or-malformed-info");
        unsigned long long identity = mock.identity;
        if (variant == 0) ++mock.identity;
        if (variant == 1) mock.info_count = THREAD_IDENTIFIER_INFO_COUNT - 1;
        if (variant == 2) mock.info_count = THREAD_IDENTIFIER_INFO_COUNT + 1;
        CHECK(ios_precedence_apply(77, identity, 7) == STATUS_INVALID_HANDLE);
        CHECK(mock.extract_calls == 0);
        expect_no_apply();
    }
}

static void malformed_policy_and_rollback(void)
{
    for (unsigned int variant = 0; variant < 3; ++variant)
    {
        begin_case("initial-policy-must-have-exact-count-and-not-default");
        if (variant == 0) mock.get[0].count = 0;
        if (variant == 1) mock.get[0].count = 2;
        if (variant == 2) mock.get[0].defaults = 1;
        CHECK(ios_precedence_apply(77, mock.identity, 7) == STATUS_UNSUCCESSFUL);
        CHECK(mock.get_calls == 1 && mock.set_calls == 0 && mock.policy == 11);
    }
    for (unsigned int variant = 0; variant < 4; ++variant)
    {
        begin_case("malformed-or-mismatched-readback-restores-prior-policy");
        if (variant == 0) mock.get[1].count = 0;
        if (variant == 1) mock.get[1].count = 2;
        if (variant == 2) mock.get[1].defaults = 1;
        if (variant == 3) { mock.get[1].override_importance = 1; mock.get[1].importance = 6; }
        CHECK(ios_precedence_apply(77, mock.identity, 7) == STATUS_UNSUCCESSFUL);
        CHECK(mock.set_calls == 2 && mock.get_calls == 3 && mock.policy == 11);
        CHECK(mock.set_values[0] == 7 && mock.set_values[1] == 11);
        CHECK(mock.extract_calls == 0 && mock.release_calls == 0);
    }
    for (unsigned int variant = 0; variant < 6; ++variant)
    {
        begin_case("failed-rollback-set-or-verification-is-never-success");
        mock.get[1].error = KERN_NO_ACCESS;
        if (variant == 0) mock.set_error[1] = KERN_TERMINATED;
        if (variant == 1) mock.get[2].error = KERN_TERMINATED;
        if (variant == 2) mock.get[2].count = 0;
        if (variant == 3) mock.get[2].count = 2;
        if (variant == 4) mock.get[2].defaults = 1;
        if (variant == 5) { mock.get[2].override_importance = 1; mock.get[2].importance = 6; }
        CHECK(ios_precedence_apply(77, mock.identity, 7) == STATUS_UNSUCCESSFUL);
        CHECK(mock.set_calls == 2 && mock.set_values[1] == 11);
        CHECK(mock.get_calls == (variant == 0 ? 2U : 3U));
        CHECK(mock.extract_calls == 0 && mock.release_calls == 0);
    }
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--gate"))
    {
        begin_case("exact-env-gate-is-cached-within-process");
        int expected = !strcmp(argv[2], "1");
        CHECK(ios_precedence_enabled() == expected);
        CHECK(setenv("MADEIRA_IOS_THREAD_PRECEDENCE", expected ? "0" : "1", 1) == 0);
        CHECK(ios_precedence_enabled() == expected);
        CHECK(mock.task_calls == 0 && mock.extract_calls == 0 && mock.info_calls == 0 && mock.set_calls == 0);
    }
    else
    {
        CHECK(argc == 1);
        successful_lifetime();
        invalid_inputs();
        error_mapping();
        malformed_identity();
        malformed_policy_and_rollback();
    }
    printf("{\"passed\":true,\"cases\":%u,\"assertions\":%u,\"realMachCalls\":false}\n", cases, assertions);
    return 0;
}
