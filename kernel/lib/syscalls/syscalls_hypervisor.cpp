// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/guest_dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/hypervisor_dispatcher.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/vm_object_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

mx_status_t sys_hypervisor_create(mx_handle_t opt_handle, uint32_t options, mx_handle_t* out) {
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t status = HypervisorDispatcher::Create(&dispatcher, &rights);
    if (status != NO_ERROR)
        return status;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    if (make_user_ptr(out).copy_to_user(up->MapHandleToValue(handle)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return NO_ERROR;
}

static mx_status_t guest_create(mx_handle_t hypervisor_handle, mx_handle_t guest_phys_mem_handle,
                                mx_handle_t* out) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<HypervisorDispatcher> hypervisor;
    mx_status_t status =
        up->GetDispatcherWithRights(hypervisor_handle, MX_RIGHT_EXECUTE, &hypervisor);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<VmObjectDispatcher> guest_phys_mem;
    status = up->GetDispatcherWithRights(
        guest_phys_mem_handle, MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE, &guest_phys_mem);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    status = GuestDispatcher::Create(hypervisor, guest_phys_mem->vmo(), &dispatcher, &rights);
    if (status != NO_ERROR)
        return status;

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    *out = up->MapHandleToValue(handle);
    up->AddHandle(mxtl::move(handle));
    return NO_ERROR;
}

static mx_status_t guest_start(mx_handle_t handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<GuestDispatcher> guest;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_EXECUTE, &guest);
    if (status != NO_ERROR)
        return status;

    return guest->Start();
}

mx_status_t sys_hypervisor_op(mx_handle_t handle, uint32_t opcode, const void* args,
                              uint32_t args_len, void* result, uint32_t result_len) {
    switch (opcode) {
    case MX_HYPERVISOR_OP_GUEST_CREATE: {
        mx_handle_t guest_phys_mem;
        if (args_len != sizeof(guest_phys_mem))
            return ERR_INVALID_ARGS;
        if (make_user_ptr(args).copy_array_from_user(&guest_phys_mem, sizeof(guest_phys_mem)) !=
            NO_ERROR)
            return ERR_INVALID_ARGS;
        mx_handle_t out;
        if (result_len != sizeof(out))
            return ERR_INVALID_ARGS;
        mx_status_t status = guest_create(handle, guest_phys_mem, &out);
        if (status != NO_ERROR)
            return status;
        if (make_user_ptr(result).copy_array_to_user(&out, sizeof(out)) != NO_ERROR)
            return ERR_INVALID_ARGS;
        return NO_ERROR;
    }
    case MX_HYPERVISOR_OP_GUEST_START: {
        return guest_start(handle);
    }
    default:
        return ERR_INVALID_ARGS;
    }
}
