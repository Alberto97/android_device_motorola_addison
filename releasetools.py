# Copyright (C) 2017 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

def FullOTA_InstallEnd(info):
  info.script.AppendExtra('if (getprop("ro.boot.radio") == "China") then')
  info.script.Print("Chinese variant detected")
  info.script.Print("Selecting NFC configuration...")
  info.script.Mount("/system")
  info.script.AppendExtra('delete("/system/etc/libnfc-nxp.conf");')
  info.script.RenameFile("/system/etc/libnfc-nxp_ds.conf","/system/etc/libnfc-nxp.conf")
  info.script.Unmount("/system")
  info.script.AppendExtra('endif;')
