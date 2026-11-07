import time
from client import DaoBridgeClient

c = DaoBridgeClient()
if not c.connect():
    print("Failed to connect!")
    exit(1)

print("=== 1. Testing Slot Tabs (0..6) ===")
slot_names = ["Chest", "Gloves", "Boots", "MainHand", "OffHand", "Helmet", "Cloak"]
for slot, name in enumerate(slot_names):
    r = c.trigger_action("DAOTransmog_SelectSlot", str(slot))
    time.sleep(0.1)
    hdr = c.inspect_ui("_root.DAOTransmog_Browser.hdr.lbl.TextField.text").get("value")
    pg = c.inspect_ui("_root.DAOTransmog_Browser.pgInfo.lbl.TextField.text").get("value")
    row0 = c.inspect_ui("_root.DAOTransmog_Browser.row0.lbl.TextField.text").get("value")
    print(f"Slot {slot} ({name}): executed={r.get('executed')}, hdr='{hdr}', pg='{pg}', row0='{row0}'")

print("\n=== 2. Testing Pagination on Chest ===")
c.trigger_action("DAOTransmog_SelectSlot", "0")
time.sleep(0.1)
pg_before = c.inspect_ui("_root.DAOTransmog_Browser.pgInfo.lbl.TextField.text").get("value")
c.trigger_action("DAOTransmog_Page", "1")
time.sleep(0.1)
pg_after = c.inspect_ui("_root.DAOTransmog_Browser.pgInfo.lbl.TextField.text").get("value")
print(f"PageNext: before='{pg_before}', after='{pg_after}'")

print("\n=== 3. Testing Material Cycling ===")
mat_before = c.inspect_ui("_root.DAOTransmog_Browser.matInfo.lbl.TextField.text").get("value")
c.trigger_action("DAOTransmog_Mat", "1")
time.sleep(0.1)
mat_after = c.inspect_ui("_root.DAOTransmog_Browser.matInfo.lbl.TextField.text").get("value")
print(f"MatNext: before='{mat_before}', after='{mat_after}'")

print("\n=== 4. Testing Presets ===")
for p in range(3):
    pr = c.trigger_action("DAOTransmog_Preset", str(p))
    time.sleep(0.1)
    p0 = c.inspect_ui("_root.DAOTransmog_Browser.pre0.lbl.TextField.text").get("value")
    p1 = c.inspect_ui("_root.DAOTransmog_Browser.pre1.lbl.TextField.text").get("value")
    p2 = c.inspect_ui("_root.DAOTransmog_Browser.pre2.lbl.TextField.text").get("value")
    print(f"Click Preset {p}: pre0='{p0}', pre1='{p1}', pre2='{p2}'")

print("\n=== 5. Testing Row Selection and Staging ===")
c.trigger_action("DAOTransmog_SelectSlot", "0")
time.sleep(0.1)
c.trigger_action("DAOTransmog_ClickRow", "0")
time.sleep(0.1)
hl0 = c.inspect_ui("_root.DAOTransmog_Browser.row0.hl._visible").get("value")
state = c.get_state()
print(f"Row 0 selected: hl0={hl0}, hero_id={state.get('hero_id')}")

c.close()
print("All interactive checks finished!")
