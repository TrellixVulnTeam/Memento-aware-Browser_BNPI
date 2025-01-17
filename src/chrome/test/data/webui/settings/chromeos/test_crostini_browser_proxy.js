// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @implements {settings.CrostiniBrowserProxy} */
class TestCrostiniBrowserProxy extends TestBrowserProxy {
  constructor() {
    super([
      'requestCrostiniInstallerView',
      'requestRemoveCrostini',
      'getCrostiniSharedPathsDisplayText',
      'getCrostiniSharedUsbDevices',
      'setCrostiniUsbDeviceShared',
      'removeCrostiniSharedPath',
      'exportCrostiniContainer',
      'importCrostiniContainer',
      'requestCrostiniContainerUpgradeView',
      'requestCrostiniUpgraderDialogStatus',
      'requestCrostiniContainerUpgradeAvailable',
      'addCrostiniPortForward',
      'getCrostiniDiskInfo',
      'resizeCrostiniDisk',
      'checkCrostiniMicSharingStatus',
      'addCrostiniPortForward',
      'removeCrostiniPortForward',
      'removeAllCrostiniPortForwards',
      'activateCrostiniPortForward',
      'deactivateCrostiniPortForward',
      'getCrostiniActivePorts',
      'checkCrostiniIsRunning',
      'shutdownCrostini',
      'setCrostiniMicSharingEnabled',
      'getCrostiniMicSharingEnabled',
    ]);
    this.sharedUsbDevices = [];
    this.removeSharedPathResult = true;
    this.crostiniMicSharingEnabled = false;
    this.crostiniIsRunning = true;
    this.methodCalls_ = {};
  }

  getNewPromiseFor(name) {
    if (name in this.methodCalls_) {
      return new Promise((resolve, reject) => {
        this.methodCalls_[name].push({name, resolve, reject});
      });
    } else {
      return new Promise((resolve, reject) => {
        this.methodCalls_[name] = [{name, resolve, reject}];
      });
    }
  }

  async resolvePromises(name, ...args) {
    await this.whenCalled(name);
    console.log(
        name + ' has been called ' + this.getCallCount(name) +
        ' times during this test');
    console.log(
        'Resolving :\'' + name + '\', ' + this.methodCalls_[name].length +
        ' times.');
    for (const o of this.methodCalls_[name]) {
      await o.resolve(...args);
    }
    this.methodCalls_[name] = [];
  }

  async rejectPromises(name, ...args) {
    for (const o of this.methodCalls_[name]) {
      await o.reject(...args);
    }
    this.methodCalls_[name] = [];
  }

  async rejectAllPromises(names) {
    for (name of names) {
      if (this.methodCalls_[name] == null) {
        console.log('\'' + name + '\' wasn\'t called during this test.');
        continue;
      }
      console.log(
          'Rejecting ' + this.methodCalls_[name].length + ' \'' + name +
          '\' promises.');
      for (const o of this.methodCalls_[name]) {
        await o.reject();
      }
      this.methodCalls_[name] = [];
    }
  }

  /** @override */
  requestCrostiniInstallerView() {
    this.methodCalled('requestCrostiniInstallerView');
  }

  /** override */
  requestRemoveCrostini() {
    this.methodCalled('requestRemoveCrostini');
  }

  /** override */
  getCrostiniSharedPathsDisplayText(paths) {
    this.methodCalled('getCrostiniSharedPathsDisplayText');
    return Promise.resolve(paths.map(path => path + '-displayText'));
  }

  /** @override */
  getCrostiniSharedUsbDevices() {
    this.methodCalled('getCrostiniSharedUsbDevices');
    return Promise.resolve(this.sharedUsbDevices);
  }

  /** @override */
  setCrostiniUsbDeviceShared(guid, shared) {
    this.methodCalled('setCrostiniUsbDeviceShared', [guid, shared]);
  }

  /** override */
  removeCrostiniSharedPath(vmName, path) {
    this.methodCalled('removeCrostiniSharedPath', [vmName, path]);
    return Promise.resolve(this.removeSharedPathResult);
  }

  /** @override */
  requestCrostiniInstallerStatus() {
    cr.webUIListenerCallback('crostini-installer-status-changed', false);
  }

  /** @override */
  requestCrostiniExportImportOperationStatus() {
    cr.webUIListenerCallback(
        'crostini-export-import-operation-status-changed', false);
  }

  /** override */
  exportCrostiniContainer() {
    this.methodCalled('exportCrostiniContainer');
  }

  /** override */
  importCrostiniContainer() {
    this.methodCalled('importCrostiniContainer');
  }

  /** @override */
  requestCrostiniContainerUpgradeView() {
    this.methodCalled('requestCrostiniContainerUpgradeView');
  }

  /** @override */
  requestCrostiniUpgraderDialogStatus() {
    cr.webUIListenerCallback('crostini-upgrader-status-changed', false);
  }

  /** @override */
  requestCrostiniContainerUpgradeAvailable() {
    cr.webUIListenerCallback(
        'crostini-container-upgrade-available-changed', true);
  }

  /** @override */
  addCrostiniPortForward(
      vmName, containerName, portNumber, protocolIndex, label) {
    this.methodCalled(
        'addCrostiniPortForward', vmName, containerName, portNumber,
        protocolIndex, label);
    return Promise.resolve(true);
  }

  /** @override */
  removeCrostiniPortForward(vmName, containerName, portNumber, protocolIndex) {
    this.methodCalled(
        'removeCrostiniPortForward', vmName, containerName, portNumber,
        protocolIndex);
    return Promise.resolve(true);
  }

  /** @override */
  activateCrostiniPortForward(
      vmName, containerName, portNumber, protocolIndex) {
    this.methodCalled(
        'activateCrostiniPortForward', vmName, containerName, portNumber,
        protocolIndex);
    return Promise.resolve(true);
  }

  /** @override */
  removeAllCrostiniPortForwards(vmName, containerName) {
    this.methodCalled('removeAllCrostiniPortForwards');
  }

  /** @override */
  getCrostiniDiskInfo(vmName, requestFullInfo) {
    this.methodCalled('getCrostiniDiskInfo', vmName, requestFullInfo);
    return this.getNewPromiseFor('getCrostiniDiskInfo');
  }

  /** @override */
  resizeCrostiniDisk(vmName, newSizeBytes) {
    this.methodCalled('resizeCrostiniDisk', vmName, newSizeBytes);
    return this.getNewPromiseFor('resizeCrostiniDisk');
  }

  /** @override */
  checkCrostiniMicSharingStatus(proposedValue) {
    this.methodCalled('checkCrostiniMicSharingStatus', proposedValue);
    return Promise.resolve(
        proposedValue !== this.crostiniMicSharingEnabled &&
        this.crostiniIsRunning);
  }

  /** @override */
  deactivateCrostiniPortForward(
      vmName, containerName, portNumber, protocolIndex) {
    this.methodCalled(
        'deactivateCrostiniPortForward', vmName, containerName, portNumber,
        protocolIndex);
    return Promise.resolve(true);
  }

  /** @override */
  getCrostiniActivePorts() {
    this.methodCalled('getCrostiniActivePorts');
    return Promise.resolve(new Array());
  }

  /** @override */
  checkCrostiniIsRunning() {
    this.methodCalled('checkCrostiniIsRunning');
    return Promise.resolve(true);
  }

  /** @override */
  shutdownCrostini() {
    this.methodCalled('shutdownCrostini');
    this.crostiniIsRunning = false;
  }

  /** @override */
  setCrostiniMicSharingEnabled(enabled) {
    this.methodCalled('setCrostiniMicSharingEnabled');
    this.crostiniMicSharingEnabled = enabled;
  }

  /** @override */
  getCrostiniMicSharingEnabled() {
    this.methodCalled('getCrostiniMicSharingEnabled');
    return Promise.resolve(this.CrostiniMicSharingEnabled);
  }
}