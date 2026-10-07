;(function () {
  const client = window.createLNbitsExtensionClient({extensionId: 'paysplit'})
  const state = {
    loading: false,
    source: null,
    targets: [],
    wallets: []
  }

  const els = {}

  document.addEventListener('DOMContentLoaded', init)

  async function init() {
    bindElements()
    bindEvents()
    await refresh()
  }

  function bindElements() {
    Object.assign(els, {
      addTargetButton: document.getElementById('addTargetButton'),
      deleteButton: document.getElementById('deleteButton'),
      enabledInput: document.getElementById('enabledInput'),
      maxAmountInput: document.getElementById('maxAmountInput'),
      notificationDropdown: document.getElementById('notificationDropdown'),
      notificationSummary: document.getElementById('notificationSummary'),
      notificationChannels: Array.from(
        document.querySelectorAll('input[name="notificationChannel"]')
      ),
      refreshButton: document.getElementById('refreshButton'),
      remainingSummary: document.getElementById('remainingSummary'),
      saveButton: document.getElementById('saveButton'),
      statusBox: document.getElementById('statusBox'),
      targetSummary: document.getElementById('targetSummary'),
      targetsBody: document.getElementById('targetsBody'),
      walletSelect: document.getElementById('walletSelect')
    })
  }

  function bindEvents() {
    els.refreshButton.addEventListener('click', refresh)
    els.addTargetButton.addEventListener('click', () => {
      state.targets.push(emptyTarget())
      renderTargets()
    })
    els.saveButton.addEventListener('click', save)
    els.deleteButton.addEventListener('click', removeSource)
    els.walletSelect.addEventListener('change', loadSelectedSource)
    els.targetsBody.addEventListener('input', onTargetInput)
    els.targetsBody.addEventListener('click', onTargetClick)
    els.notificationDropdown.addEventListener('change', updateNotificationSummary)
    document.addEventListener('click', event => {
      if (!els.notificationDropdown.contains(event.target)) {
        els.notificationDropdown.open = false
      }
    })
    els.notificationDropdown.addEventListener('keydown', event => {
      if (event.key === 'Escape') {
        els.notificationDropdown.open = false
        els.notificationDropdown.querySelector('summary').focus()
      }
    })
  }

  async function refresh() {
    setLoading(true)
    try {
      const data = await client.listWallets()
      state.wallets = Array.isArray(data?.wallets) ? data.wallets : []
      renderWalletOptions()
      await loadSelectedSource()
      setStatus('Wallets refreshed.', 'positive')
    } catch (error) {
      console.error('[paysplit admin] Refresh failed.', error)
      setStatus(error.message || String(error), 'negative')
      await client.notify('negative', error)
    } finally {
      setLoading(false)
    }
  }

  function renderWalletOptions() {
    els.walletSelect.innerHTML = ''
    for (const wallet of state.wallets) {
      const option = document.createElement('option')
      option.value = wallet.id
      option.textContent = wallet.name || wallet.id
      els.walletSelect.appendChild(option)
    }
  }

  async function loadSelectedSource() {
    const walletId = selectedWalletId()
    if (!walletId) {
      state.source = null
      state.targets = []
      renderSource()
      renderTargets()
      return
    }

    setLoading(true)
    try {
      const data = await client.getSource(walletId)
      state.source = data?.source || null
      state.targets = Array.isArray(data?.targets) ? data.targets : []
      if (!state.targets.length) {
        state.targets = [emptyTarget()]
      }
      renderSource()
      renderTargets()
    } catch (error) {
      console.error('[paysplit admin] Source load failed.', {walletId, error})
      setStatus(error.message || String(error), 'negative')
      await client.notify('negative', error)
    } finally {
      setLoading(false)
    }
  }

  function renderSource() {
    els.enabledInput.checked = state.source?.enabled !== false
    els.maxAmountInput.value = state.source?.max_amount || ''
    const channels = state.source?.notification_channels || []
    for (const input of els.notificationChannels) {
      input.checked = channels.includes(input.value)
    }
    els.notificationDropdown.open = false
    updateNotificationSummary()
  }

  function updateNotificationSummary() {
    els.notificationSummary.textContent =
      els.notificationChannels
        .filter(input => input.checked)
        .map(input => input.parentElement.textContent.trim())
        .join(', ') || 'None'
  }

  function renderTargets() {
    els.targetsBody.innerHTML = ''
    for (const [index, target] of state.targets.entries()) {
      const row = document.createElement('tr')
      row.dataset.index = String(index)
      row.innerHTML = `
        <td>
          <input
            class="target-alias"
            value="${htmlAttr(target.alias || '')}"
            placeholder="Platform, savings, team..."
          />
        </td>
        <td>
          <input
            class="target-lnurl"
            value="${htmlAttr(target.lnurl || '')}"
            placeholder="name@example.com or LNURL..."
          />
        </td>
        <td>
          <input
            class="target-percent"
            type="number"
            min="1"
            max="100"
            step="1"
            value="${htmlAttr(target.percent || '')}"
          />
        </td>
        <td>
          <button class="paysplit-icon-button target-remove" type="button">
            <span class="material-icons">delete</span>
          </button>
        </td>
      `
      els.targetsBody.appendChild(row)
    }
    updateSummary()
  }

  function onTargetInput(event) {
    const row = event.target.closest('tr')
    if (!row) return

    const index = Number(row.dataset.index)
    const target = state.targets[index]
    if (!target) return

    target.alias = row.querySelector('.target-alias').value.trim()
    target.lnurl = row.querySelector('.target-lnurl').value.trim()
    target.percent = numericValue(row.querySelector('.target-percent').value)
    updateSummary()
  }

  function onTargetClick(event) {
    const removeButton = event.target.closest('.target-remove')
    if (!removeButton) return

    const row = removeButton.closest('tr')
    const index = Number(row.dataset.index)
    state.targets.splice(index, 1)
    if (!state.targets.length) {
      state.targets.push(emptyTarget())
    }
    renderTargets()
  }

  async function save() {
    const wallet = selectedWallet()
    if (!wallet) {
      setStatus('Select a wallet first.', 'negative')
      return
    }

    const payload = collectPayload(wallet)
    const validationError = validatePayload(payload)
    if (validationError) {
      setStatus(validationError, 'negative')
      return
    }

    setLoading(true)
    try {
      await client.requestWalletPaymentWatchPermission({
        walletId: wallet.id
      })
      await client.requestBackgroundPaymentPermission({
        walletId: wallet.id,
        maxAmount: payload.maxAmount || 1000,
        destinationPolicy: 'external_allowed'
      })
      const data = await client.saveSource(payload)
      state.source = data?.source || null
      state.targets = Array.isArray(data?.targets) ? data.targets : []
      renderSource()
      renderTargets()
      setStatus('PaySplit configuration saved.', 'positive')
      await client.notify('positive', 'PaySplit configuration saved.')
    } catch (error) {
      console.error('[paysplit admin] Save failed.', {payload, error})
      setStatus(error.message || String(error), 'negative')
      await client.notify('negative', error)
    } finally {
      setLoading(false)
    }
  }

  async function removeSource() {
    const walletId = selectedWalletId()
    if (!walletId) return
    if (!window.confirm('Delete PaySplit config for this wallet?')) return

    setLoading(true)
    try {
      await client.deleteSource(walletId)
      state.source = null
      state.targets = [emptyTarget()]
      renderSource()
      renderTargets()
      setStatus('PaySplit configuration deleted.', 'positive')
      await client.notify('positive', 'PaySplit configuration deleted.')
    } catch (error) {
      console.error('[paysplit admin] Delete failed.', {walletId, error})
      setStatus(error.message || String(error), 'negative')
      await client.notify('negative', error)
    } finally {
      setLoading(false)
    }
  }

  function selectedWalletId() {
    return els.walletSelect.value
  }

  function selectedWallet() {
    const walletId = selectedWalletId()
    return state.wallets.find(wallet => wallet.id === walletId) || null
  }

  function collectPayload(wallet) {
    const targets = state.targets
      .map(target => ({
        id: target.id || '',
        alias: String(target.alias || '').trim(),
        lnurl: String(target.lnurl || '').trim(),
        percent: numericValue(target.percent)
      }))
      .filter(target => target.alias || target.lnurl || target.percent)

    return {
      enabled: Boolean(els.enabledInput.checked),
      maxAmount: numericValue(els.maxAmountInput.value),
      notificationChannels: els.notificationChannels
        .filter(input => input.checked)
        .map(input => input.value),
      targets,
      walletId: wallet.id,
      walletName: wallet.name || wallet.id
    }
  }

  function validatePayload(payload) {
    if (!payload.walletId) return 'Select a source wallet.'
    let total = 0
    for (const target of payload.targets) {
      if (!target.alias) return 'Each target needs an alias.'
      if (!looksLikeLnurlTarget(target.lnurl)) {
        return 'Each target needs a Lightning Address or LNURL-pay.'
      }
      if (!target.percent || target.percent <= 0 || target.percent > 100) {
        return 'Each target percentage must be between 1 and 100.'
      }
      total += target.percent
    }
    if (total > 100) return 'Total split percentage cannot exceed 100.'
    return ''
  }

  function looksLikeLnurlTarget(value) {
    const text = String(value || '').trim()
    return (
      text.includes('@') ||
      text.toLowerCase().startsWith('lnurl') ||
      text.toLowerCase().startsWith('lightning:')
    )
  }

  function updateSummary() {
    const total = state.targets.reduce(
      (sum, target) => sum + numericValue(target.percent),
      0
    )
    const remaining = Math.max(0, 100 - total)
    els.targetSummary.textContent = `${total}% configured`
    els.remainingSummary.textContent = `${remaining}% remains in source wallet`
    els.targetSummary.classList.toggle('negative', total > 100)
  }

  function emptyTarget() {
    return {
      alias: '',
      id: '',
      lnurl: '',
      percent: ''
    }
  }

  function numericValue(value) {
    const number = Number(value)
    return Number.isFinite(number) && number > 0 ? number : 0
  }

  function setLoading(loading) {
    state.loading = loading
    els.saveButton.disabled = loading
    els.deleteButton.disabled = loading
    els.refreshButton.disabled = loading
    els.addTargetButton.disabled = loading
  }

  function setStatus(message, level) {
    els.statusBox.hidden = !message
    els.statusBox.textContent = message || ''
    els.statusBox.dataset.level = level || 'info'
  }

  function htmlAttr(value) {
    return String(value)
      .replace(/&/g, '&amp;')
      .replace(/"/g, '&quot;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
  }
})()
